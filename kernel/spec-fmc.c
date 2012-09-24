/*
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 *
 * This work is part of the White Rabbit project, a research effort led
 * by CERN, the European Institute for Nuclear Research.
 */
#include <linux/slab.h>
#include <linux/fmc.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include <linux/fmc-sdb.h>
#include "spec.h"

static int spec_test_irq;
module_param_named(test_irq, spec_test_irq, int, 0444);

/* The main role of this file is offering the fmc_operations for the spec */

static int spec_validate(struct fmc_device *fmc, struct fmc_driver *drv)
{
	struct spec_dev *spec = fmc->carrier_data;
	struct pci_dev *pdev = spec->pdev;
	int busid = (pdev->bus->number << 8) | pdev->devfn;
	int i;

	if (!drv->busid_n)
		return 0; /* everyhing is valid */
	for (i = 0; i < drv->busid_n; i++)
		if (drv->busid_val[i] == busid)
			return i;
	return -ENOENT;
}

static int spec_reprogram(struct fmc_device *fmc, struct fmc_driver *drv,
			  char *gw)
{
	const struct firmware *fw;
	struct spec_dev *spec = fmc->carrier_data;
	struct device *dev = fmc->hwdev;
	uint32_t reg;
	int ret;

	if (!gw)
		gw = spec_fw_name;

	if (!strlen(gw)) { /* use module parameters from the driver */
		int index;

		index = spec_validate(fmc, drv);

		gw = drv->gw_val[index];
		if (!gw)
			return -ESRCH; /* the caller may accept this */
	}

	dev_info(fmc->hwdev, "reprogramming with %s\n", gw);
	ret = request_firmware(&fw, gw, dev);
	if (ret < 0) {
		dev_warn(dev, "request firmware \"%s\": error %i\n", gw, ret);
		goto out;
	}
	fmc_free_sdb_tree(fmc);
	fmc->flags &= ~(FMC_DEVICE_HAS_GOLDEN | FMC_DEVICE_HAS_CUSTOM);
	ret = spec_load_fpga(spec, fw->data, fw->size);
	if (ret <0) {
		dev_err(dev, "write firmware \"%s\": error %i\n", gw, ret);
		goto out;
	}
	if (gw == spec_fw_name)
		fmc->flags |= FMC_DEVICE_HAS_GOLDEN;
	else
		fmc->flags |= FMC_DEVICE_HAS_CUSTOM;

	/* After reprogramming, reset the FPGA using the gennum register */
	reg = gennum_readl(spec, GNPCI_SYS_CFG_SYSTEM);
	/*
	 * This _fucking_ register must be written with extreme care,
	 * becase some fields are "protected" and some are not. *hate*
	 */
	gennum_writel(spec, (reg & ~0xffff) | 0x3fff, GNPCI_SYS_CFG_SYSTEM);
	gennum_writel(spec, (reg & ~0xffff) | 0x7fff, GNPCI_SYS_CFG_SYSTEM);

out:
	release_firmware(fw);
	return ret;
}

static int spec_irq_request(struct fmc_device *fmc, irq_handler_t handler,
			    char *name, int flags)
{
	struct spec_dev *spec = fmc->carrier_data;
	int ret;
	u32 value;

	ret = request_irq(fmc->irq, handler, flags, name, fmc);
	if (ret) return ret;

	value = gennum_readl(spec, GNPPCI_MSI_CONTROL);
	if ((value & 0x810000) != 0x810000)
		dev_err(&spec->pdev->dev, "invalid msi control: 0x%08x\n",
			value);
	value = 0xa50000 | (value & 0xffff);
	gennum_writel(spec, value, GNPPCI_MSI_CONTROL);

	/* Enable gpio interrupts:
	 * gpio6: tp8: output low
	 * gpio7: tp7: input (was: interrupt, raising edge
	 * gpio8: IRQ1 from FPGA: interrupt, raising edge
	 * gpio9: IRQ0 from FPGA: input (was: interrupt, raising edge)
	 * gpio10: tp6: output low
	 * gpio11: tp5: input (was: interrupt, raising edge)
	 */

	/* bypass = alternate function */
	gennum_mask_val(spec, 0xfc0, 0x00, GNGPIO_BYPASS_MODE);
	/* direction 0 = output */
	gennum_mask_val(spec, 0x440, 0x000, GNGPIO_DIRECTION_MODE);
	gennum_mask_val(spec, 0xb80, 0xb80, GNGPIO_DIRECTION_MODE);
	gennum_mask_val(spec, 0x440, 0x440, GNGPIO_OUTPUT_ENABLE);

	gennum_mask_val(spec, 0x100, 0x000, GNGPIO_INT_TYPE); /* 0 = edge */
	gennum_mask_val(spec, 0x100, 0x100, GNGPIO_INT_VALUE); /* 1 = raising */
	gennum_mask_val(spec, 0x100, 0x000, GNGPIO_INT_ON_ANY);

	gennum_writel(spec, 0x100, GNGPIO_INT_MASK_CLR); /* enable */
	return 0;
}

static void spec_irq_ack(struct fmc_device *fmc)
{
	struct spec_dev *spec = fmc->carrier_data;

	/*
	 * Note: we only support gpio interrupts here, i.e. the
	 * 0x814 (INT_STAT) register is expected to only have bit 15 set.
	 * We also accept software-generated irq, but they need no ack.
	 */
	gennum_readl(spec, GNGPIO_INT_STATUS);
}

static int spec_irq_free(struct fmc_device *fmc)
{
	struct spec_dev *spec = fmc->carrier_data;

	gennum_writel(spec, 0xffff, GNGPIO_INT_MASK_SET); /* disable */
	free_irq(fmc->irq, fmc);
	return 0;
}

/* The engines for this live in spec-i2c.c, we only shape arguments */
static int spec_read_ee(struct fmc_device *fmc, int pos, void *data, int len)
{
	if (!(fmc->flags & FMC_DEVICE_HAS_GOLDEN))
		return -ENOTSUPP;
	return spec_eeprom_read(fmc, SPEC_I2C_EEPROM_ADDR, pos, data, len);
}

static int spec_write_ee(struct fmc_device *fmc, int pos,
			 const void *data, int len)
{
	if (!(fmc->flags & FMC_DEVICE_HAS_GOLDEN))
		return -ENOTSUPP;
	return spec_eeprom_write(fmc, SPEC_I2C_EEPROM_ADDR, pos, data, len);
}

static struct fmc_operations spec_fmc_operations = {
	/* no readl/writel because we have the base pointer */
	.validate =		spec_validate,
	.reprogram =		spec_reprogram,
	.irq_request =		spec_irq_request,
	.irq_ack =		spec_irq_ack,
	.irq_free =		spec_irq_free,
	.read_ee =		spec_read_ee,
	.write_ee =		spec_write_ee,
};

/*
 * Since interrupts are a hairy thing with the gennum, make a test run
 * of interrupt handling using its own internal "software interrupt"
 */

static irqreturn_t spec_test_handler(int irq, void *dev_id)
{
	struct fmc_device *fmc = dev_id;
	struct spec_dev *spec = fmc->carrier_data;

	printk("got %i!\n", irq);
	spec->irq_count++;
	complete(&spec->compl);
	fmc->op->irq_ack(fmc);
	return IRQ_HANDLED;
}

/*
 * Finally, the real init and exit
 */
static int spec_irq_init(struct fmc_device *fmc)
{
	struct spec_dev *spec = fmc->carrier_data;
	uint32_t value;
	int i;

	/*
	 * Enable multiple-msi to work around a chip design bug.
	 * See http://blog.tftechpages.com/?p=595
	 */
	value = gennum_readl(spec, GNPPCI_MSI_CONTROL);
	if ((value & 0x810000) != 0x810000)
		dev_err(&spec->pdev->dev, "invalid msi control: 0x%08x\n",
			value);
	value = 0xa50000 | (value & 0xffff);
	gennum_writel(spec, value, GNPPCI_MSI_CONTROL);

	/*
	 * Now check the two least-significant bits of the msi-data register,
	 * then enable CFG_0 or .. CFG_3 accordingly, to get proper vector.
	 */
	value = gennum_readl(spec, GNPPCI_MSI_DATA);
	for (i = 0; i < 7; i++)
		gennum_writel(spec, 0, GNINT_CFG(i));
	gennum_writel(spec, 0x800c, GNINT_CFG(value & 0x03));

	/* Finally, ensure we are able to receive it -- if the user asked to */
	if (spec_test_irq == 0)
		return 0;
	spec->irq_count = 0;
	init_completion(&spec->compl);
	fmc->op->irq_request(fmc, spec_test_handler, "spec-test", 0);
	gennum_writel(spec, 8, GNINT_STAT);
	gennum_writel(spec, 0, GNINT_STAT);
	wait_for_completion_timeout(&spec->compl, msecs_to_jiffies(50));
	fmc->op->irq_free(fmc);
	if (!spec->irq_count) {
		dev_err(&spec->pdev->dev, "Can't receive interrupt\n");
		return -EIO;
	}
	dev_info(&spec->pdev->dev, "Interrupts work as expected\n");

	/* FIXME: configure the GPIO pins to receive interrupts */

	return 0;
}

static void spec_irq_exit(struct fmc_device *fmc)
{
	struct spec_dev *spec = fmc->carrier_data;
	int i;

	for (i = 0; i < 7; i++)
		gennum_writel(spec, 0, GNINT_CFG(i));
	fmc->op->irq_ack(fmc); /* just to be safe */
}

static int check_golden(struct fmc_device *fmc)
{
	struct spec_dev *spec = fmc->carrier_data;
	int ret;

	/* poor man's SDB */
	if (fmc_readl(fmc, 0x100) != 0x5344422d) {
		dev_err(&spec->pdev->dev, "Can't find SDB magic\n");
		return -ENODEV;
	}
	if ( (ret = fmc_scan_sdb_tree(fmc, 0x100)) < 0)
		return -ENODEV;

	if (fmc_readl(fmc, 0x15c) != 0x0000ce42) {
		dev_err(&spec->pdev->dev, "unsexpected vendor in SDB\n");
		return -ENODEV;
	}
	if (fmc_readl(fmc, 0x160) != 0xff07fc47) {
		dev_err(&spec->pdev->dev, "unsexpected device in SDB\n");
		return -ENODEV;
	}
	fmc_show_sdb_tree(fmc);
	return 0;
}


int spec_fmc_create(struct spec_dev *spec)
{
	struct fmc_device *fmc;
	int ret;

	fmc = kzalloc(sizeof(*fmc), GFP_KERNEL);
	if (!fmc)
		return -ENOMEM;

	/* FIXME: many fields of the device are still NULL */
	fmc->carrier_name = "SPEC";
	fmc->carrier_data = spec;
	fmc->base = spec->remap[0]; /* 1M window at offset 0 */
	fmc->irq = spec->pdev->irq;
	fmc->op = &spec_fmc_operations;
	fmc->hwdev = &spec->pdev->dev; /* for messages */
	spec->fmc = fmc;

	/* Check that the golden binary is actually correct */
	ret = check_golden(fmc);
	if (ret)
		goto out_free;

	ret = spec_i2c_init(fmc);
	if (ret)
		goto out_free;
	ret = spec_irq_init(fmc);
	if (ret)
		goto out_free;
	ret = fmc_device_register(fmc);
	if (ret)
		goto out_irq;
	spec_gpio_init(fmc); /* May fail, we don't care */
	return ret;

out_irq:
	spec_irq_exit(fmc);
out_free:
	spec->fmc = NULL;
	kfree(fmc);
	return ret;
}

void spec_fmc_destroy(struct spec_dev *spec)
{
	spec_gpio_exit(spec->fmc);
	fmc_device_unregister(spec->fmc);
	spec_irq_exit(spec->fmc);
	spec_i2c_exit(spec->fmc);
	spec->fmc = NULL;
}
