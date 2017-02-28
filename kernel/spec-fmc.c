/*
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 *
 * This work is part of the White Rabbit project, a research effort led
 * by CERN, the European Institute for Nuclear Research.
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fmc.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include <linux/gpio.h>
#include <linux/fmc-sdb.h>
#include "spec.h"

static int spec_test_irq;
module_param_named(test_irq, spec_test_irq, int, 0444);

static int spec_show_sdb;
module_param_named(show_sdb, spec_show_sdb, int, 0444);

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

static int spec_reprogram_raw(struct fmc_device *fmc, struct fmc_driver *drv,
			      void *gw, unsigned long len)
{
	struct spec_dev *spec = fmc->carrier_data;
	struct device *dev = fmc->hwdev;
	int ret;

	if (!gw || !len) {
		dev_err(dev, "Invalid firmware buffer - buf: %p len: %ld\n",
			gw, len);
		return -EINVAL;
	}

	if (!drv)
		dev_info(dev, "Carrier FPGA re-program\n");

	fmc_free_sdb_tree(fmc);

	fmc->flags &= ~FMC_DEVICE_HAS_GOLDEN;

	ret = spec_load_fpga(spec, gw, len);
	if (ret < 0) {
		dev_err(dev, "writing firmware: error %i\n", ret);
		return ret;
	}

	fmc->flags |= FMC_DEVICE_HAS_CUSTOM;
	return 0;
}

static int spec_reprogram(struct fmc_device *fmc, struct fmc_driver *drv,
			  char *gw)
{
	const struct firmware *fw;
	struct device *dev = fmc->hwdev;
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

	ret = spec_reprogram_raw(fmc, drv, (void *)fw->data, fw->size);
	if (ret < 0) {
		dev_err(dev, "write firmware \"%s\": error %i\n", gw, ret);
		goto out;
	}

	if (gw == spec_fw_name)
		fmc->flags |= FMC_DEVICE_HAS_GOLDEN;

out:
	release_firmware(fw);
	return ret;
}

/* Low-level IRQ request function: set up handler, with or without the VIC */
static int spec_shared_irq_request(struct fmc_device *fmc,
				   irq_handler_t handler, char *name, int flags)
{
	struct spec_dev *spec = fmc->carrier_data;
	int ret;
	u32 value;

	ret = request_irq(spec->pdev->irq, handler, flags, name, fmc);
	if (ret)
		return ret;

	if (spec_use_msi) {
		/* A check and a hack, but doesn't work on all computers */
		value = gennum_readl(spec, GNPPCI_MSI_CONTROL);
		if ((value & 0x810000) != 0x810000)
			dev_err(&spec->pdev->dev,
				"invalid msi control: 0x%04x\n",
				value >> 16);
		value = 0xa50000 | (value & 0xffff);
		gennum_writel(spec, value, GNPPCI_MSI_CONTROL);
	}

	/* Interrupts are enabled by the driver, with gpio_config() */
	return 0;
}

static void spec_shared_irq_ack(struct fmc_device *fmc);

static irqreturn_t spec_vic_irq_handler(int id, void *data)
{
	struct fmc_device *fmc = (struct fmc_device *)data;
	struct spec_dev *spec = (struct spec_dev *)fmc->carrier_data;
	irqreturn_t rv;

	/*
	 * Lock to avoid to remove the VIC or the handler itself while
	 * it's running
	 */
	spin_lock(&spec->irq_lock);
	rv = spec_vic_irq_dispatch(spec);

	/*
	 * We ack the GN4124 **before** the VIC. The other way around makes the
	 * interrupt handling timing dependent. For instance. When the VIC
	 * VIC_CTL_EMU_LEN_W is too short it may happens that the VIC asserts
	 * the interrupt **before** the GN4124 is acked; then, so do not receive
	 * interrupts anymore from the GN4124 because it misses the VIC edge.
	 */
	spec_shared_irq_ack(fmc);
	spec_vic_irq_ack(spec, 0);
	spin_unlock(&spec->irq_lock);

	return IRQ_HANDLED;
}

static struct fmc_gpio spec_vic_gpio_cfg[] = {
	{
		.gpio = FMC_GPIO_IRQ(1),
		.mode = GPIOF_DIR_IN,
		.irqmode = IRQF_TRIGGER_RISING,
	},

	{
		.gpio = FMC_GPIO_IRQ(0),
		.mode = GPIOF_DIR_IN,
		.irqmode = IRQF_TRIGGER_RISING,
	}
};

static int spec_irq_request(struct fmc_device *fmc, irq_handler_t handler,
			    char *name, int flags)
{
	struct spec_dev *spec = fmc->carrier_data;
	int rv, first_time;

	/* VIC mode interrupt */
	if (!(flags & IRQF_SHARED)) {
		/*
		 * Lock to serialize request/free and have a consistent status
		 * during these operations
		 */
		spin_lock(&spec->irq_lock);
		first_time = !spec->vic;

		/* configure the VIC */
		rv = spec_vic_irq_request(spec, fmc, fmc->irq, handler);
		spin_unlock(&spec->irq_lock);
		if (rv)
			return rv;

		/* on first IRQ, configure VIC "master" handler and GPIO too */
		if (first_time) {
			rv = spec_shared_irq_request(fmc, spec_vic_irq_handler,
						     "spec-vic", IRQF_SHARED);
			if (rv)
				return rv;

			fmc->op->gpio_config(fmc, spec_vic_gpio_cfg,
					     ARRAY_SIZE(spec_vic_gpio_cfg));
		}
	} else {
		rv = spec_shared_irq_request(fmc, handler, name, flags);
		pr_debug("Requesting irq '%s' in shared mode (rv %d)\n", name,
		       rv);
	}

	if (!rv)
		spec->flags |= SPEC_FLAG_IRQS_REQUESTED;

	return rv;
}

static void spec_shared_irq_ack(struct fmc_device *fmc)
{
	struct spec_dev *spec = fmc->carrier_data;

	/*
	 * Note: we only support gpio interrupts here, i.e. the
	 * 0x814 (INT_STAT) register is expected to only have bit 15 set.
	 * We also accept software-generated irq, but they need no ack.
	 */
	gennum_readl(spec, GNGPIO_INT_STATUS);
}

static void spec_irq_ack(struct fmc_device *fmc)
{
	struct spec_dev *spec = fmc->carrier_data;

	if (!spec->vic)
		spec_shared_irq_ack(fmc);

	/* Nothing for VIC here, all irqs are acked by master VIC handler */
}

static void spec_shared_irq_free(struct fmc_device *fmc)
{
	struct spec_dev *spec = fmc->carrier_data;

	gennum_writel(spec, 0xffff, GNGPIO_INT_MASK_SET);	/* disable */
	free_irq(spec->pdev->irq, fmc);
}

static int spec_irq_free(struct fmc_device *fmc)
{
	struct spec_dev *spec = fmc->carrier_data;

	/*
	 * Lock to serialize request/free and have a consistent status
	 * during these operations. And, to avoid to run VIC handler while
	 * it is going to desappear
	 */
	spin_lock(&spec->irq_lock);
	if (spec->vic)
		spec_vic_irq_free(spec, fmc->irq);

	/*
	 * If we were not using the VIC, or we released all the VIC handler,
	 * then release the PCI IRQ handler
	 */
	if (!spec->vic)
		spec_shared_irq_free(fmc);
	spin_unlock(&spec->irq_lock);
	return 0;
}

/* This is the mapping from virtual GPIO pin numbers to raw gpio numbers */
struct {
	int virtual; int raw;
} spec_gpio_map[] = {
	/*  0: TCK */
	/*  1: TMS */
	/*  2: TDO */
	/*  3: TDI */
	/*  4: SDA */
	/*  5: SCL */
	/*  6: TP8 */ {FMC_GPIO_TP(3), FMC_GPIO_RAW(6)},
	/*  7: TP7 */ {FMC_GPIO_TP(2), FMC_GPIO_RAW(7)},
	/*  8: IRQ */ {FMC_GPIO_IRQ(0), FMC_GPIO_RAW(8)},
	/*  9: IRQ */ {FMC_GPIO_IRQ(1), FMC_GPIO_RAW(9)},
	/* 10: TP6 */ {FMC_GPIO_TP(1), FMC_GPIO_RAW(10)},
	/* 11: TP5 */ {FMC_GPIO_TP(0), FMC_GPIO_RAW(11)},
	/* 12: flash_cs, 13: spri_din, 14: bootsel1, 15: bootsel0 */
};

static int spec_map_pin(int virtual)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(spec_gpio_map); i++)
		if (spec_gpio_map[i].virtual == virtual)
			return spec_gpio_map[i].raw;
	return -ENOENT;
}

static int spec_cfg_pin(struct fmc_device *fmc, int pin, int mode, int imode)
{
	struct spec_dev *spec = fmc->carrier_data;
	int valid_bits = GPIOF_DIR_IN | GPIOF_DIR_OUT
		| GPIOF_INIT_HIGH | GPIOF_INIT_LOW;
	int ret = 0;
	int bit = (1 << pin);

	if (pin < 0 || pin > 15)
		return -ENODEV;
	if (mode & ~valid_bits)
		return -EINVAL;
	if (mode & GPIOF_DIR_IN) {
		/* 1 = input */
		gennum_mask_val(spec, bit, 0, GNGPIO_OUTPUT_ENABLE);
		gennum_mask_val(spec, bit, bit, GNGPIO_DIRECTION_MODE);
		ret = !!(gennum_readl(spec, GNGPIO_INPUT_VALUE) & bit);
	} else {
		if (mode & GPIOF_INIT_HIGH)
			gennum_mask_val(spec, bit, bit, GNGPIO_OUTPUT_VALUE);
		else
			gennum_mask_val(spec, bit, 0, GNGPIO_OUTPUT_VALUE);
		gennum_mask_val(spec, bit, 0, GNGPIO_DIRECTION_MODE);
		gennum_mask_val(spec, bit, bit, GNGPIO_OUTPUT_ENABLE);
	}

	/* Then, interrupt configuration, if needed */
	if (!(imode & IRQF_TRIGGER_MASK)) {
		gennum_writel(spec, bit, GNGPIO_INT_MASK_SET); /* disable */
		return ret;
	}

	if (imode & (IRQF_TRIGGER_HIGH | IRQF_TRIGGER_RISING))
		gennum_mask_val(spec, bit, bit, GNGPIO_INT_VALUE);
	else
		gennum_mask_val(spec, bit, 0, GNGPIO_INT_VALUE);

	if (imode & (IRQF_TRIGGER_HIGH | IRQF_TRIGGER_LOW))
		gennum_mask_val(spec, bit, bit, GNGPIO_INT_TYPE);
	else
		gennum_mask_val(spec, bit, 0, GNGPIO_INT_TYPE);

	gennum_mask_val(spec, bit, 0, GNGPIO_INT_ON_ANY); /* me lazy */

	gennum_writel(spec, bit, GNGPIO_INT_MASK_CLR); /* enable */
	return ret;
}

static int spec_gpio_config(struct fmc_device *fmc, struct fmc_gpio *gpio,
			    int ngpio)
{
	int i, done = 0, retval = 0;

	for ( ; ngpio; gpio++, ngpio--) {

		if (gpio->carrier_name && strcmp(gpio->carrier_name, "SPEC")) {
			/* The array may setup raw pins for various carriers */
			continue;
		}
		if (gpio->carrier_name) {
			/* so, it's ours */
			gpio->_gpio = gpio->gpio;
		} else if (!gpio->_gpio) {
			/* virtual but not mapped (or poor gpio0) */
			i = spec_map_pin(gpio->gpio);
			if (i < 0)
				return i;
			gpio->_gpio = i;
		}

		i = spec_cfg_pin(fmc, gpio->_gpio,
				    gpio->mode, gpio->irqmode);
		if (i < 0)
			return i;
		retval += i; /* may be the input value */
		done++;
	}
	if (!done)
		return -ENODEV;
	return retval;
}


/* The engines for this live in spec-i2c.c, we only shape arguments */
static int spec_read_ee(struct fmc_device *fmc, int pos, void *data, int len)
{
	if (!(fmc->flags & FMC_DEVICE_HAS_GOLDEN))
		return -ENOTSUPP;
	return spec_eeprom_read(fmc, pos, data, len);
}

static int spec_write_ee(struct fmc_device *fmc, int pos,
			 const void *data, int len)
{
	if (!(fmc->flags & FMC_DEVICE_HAS_GOLDEN))
		return -ENOTSUPP;
	return spec_eeprom_write(fmc, pos, data, len);
}

static struct fmc_operations spec_fmc_operations = {
	/* no readl/writel because we have the base pointer */
	.validate =		spec_validate,
	.reprogram_raw =        spec_reprogram_raw,
	.reprogram =		spec_reprogram,
	.irq_request =		spec_irq_request,
	.irq_ack =		spec_irq_ack,
	.irq_free =		spec_irq_free,
	.gpio_config =		spec_gpio_config,
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

	dev_info(fmc->hwdev, "received interrupt %i\n", irq);
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

	spin_lock_init(&spec->irq_lock);
	if (spec_use_msi) {
		/*
		 * Enable multiple-msi to work around a chip design bug.
		 * See http://blog.tftechpages.com/?p=595
		 */
		value = gennum_readl(spec, GNPPCI_MSI_CONTROL);
		if ((value & 0x810000) != 0x810000)
			dev_err(&spec->pdev->dev,
				"invalid msi control: 0x%04x\n",
				value >> 16);
		value = 0xa50000 | (value & 0xffff);
		gennum_writel(spec, value, GNPPCI_MSI_CONTROL);
	}

	/*
	 * Now check the two least-significant bits of the msi-data register,
	 * then enable CFG_0 or .. CFG_3 accordingly, to get proper vector.
	 */
	value = gennum_readl(spec, GNPPCI_MSI_DATA);
	for (i = 0; i < 7; i++)
		gennum_writel(spec, 0, GNINT_CFG(i));
	if (spec_use_msi)
		gennum_writel(spec, 0x800c, GNINT_CFG(value & 0x03));
	else
		gennum_writel(spec, 0x800c, GNINT_CFG(0 /* first one */));

	/* Finally, ensure we are able to receive it -- if the user asked to */
	if (spec_test_irq == 0)
		return 0;
	spec->irq_count = 0;
	init_completion(&spec->compl);
	fmc->op->irq_request(fmc, spec_test_handler, "spec-test", IRQF_SHARED);
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

	WARN(spec->vic, "A Mezzanine driver didn't release all its IRQ handlers\n");
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

	ret = fmc_scan_sdb_tree(fmc, 0x100);
	if (ret < 0)
		return -ENODEV;

	if (fmc_readl(fmc, 0x15c) != 0x0000ce42) {
		dev_err(&spec->pdev->dev, "unsexpected vendor in SDB\n");
		return -ENODEV;
	}
	if (fmc_readl(fmc, 0x160) != 0xff07fc47) {
		dev_err(&spec->pdev->dev, "unsexpected device in SDB\n");
		return -ENODEV;
	}
	if (spec_show_sdb)
		fmc_show_sdb_tree(fmc);
	return 0;
}


int spec_fmc_create(struct spec_dev *spec, struct fmc_gateware *gw)
{
	struct fmc_device *fmc;
	struct pci_dev *pdev;
	int ret;

	fmc = kzalloc(sizeof(*fmc), GFP_KERNEL);
	if (!fmc)
		return -ENOMEM;

	fmc->version = FMC_VERSION;
	fmc->owner = THIS_MODULE;
	fmc->carrier_name = "SPEC";
	fmc->carrier_data = spec;

	/* 1M window at offset 0 */
	fmc->fpga_base = spec->remap[0];
	fmc->memlen = 1 << 20;

	fmc->op = &spec_fmc_operations;
	fmc->hwdev = &spec->pdev->dev; /* for messages */
	spec->fmc = fmc;

	/* We have one slot only, and the i2c address is mandated */
	fmc->slot_id = 0;
	fmc->eeprom_addr = SPEC_I2C_EEPROM_ADDR;

	/* The device id is needed to build mezzanine unique names */
	pdev = spec->pdev;
	fmc->device_id = (pdev->bus->number << 8) | pdev->devfn;

	/* Check that the golden binary is actually correct */
	ret = check_golden(fmc);
	if (ret)
		goto out_free;
	fmc->flags = FMC_DEVICE_HAS_GOLDEN;

	ret = spec_i2c_init(fmc);
	if (ret)
		goto out_free;
	ret = spec_irq_init(fmc);
	if (ret)
		goto out_free;
	ret = fmc_device_register_gw(fmc, gw);
	if (ret)
		goto out_irq;
	spec_gpio_init(fmc); /* May fail, we don't care */

	spec->flags |= SPEC_FLAG_FMC_REGISTERED;
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
	if (!(spec->flags & SPEC_FLAG_FMC_REGISTERED))
		return;
	/* undo the things in the reverse order, but pin the device first */
	get_device(&spec->fmc->dev);
	spec_gpio_exit(spec->fmc);
	fmc_device_unregister(spec->fmc);
	spec_irq_exit(spec->fmc);
	spec_i2c_exit(spec->fmc);
	put_device(&spec->fmc->dev);
	/*
	 * Do not do `kfree(spec->fmc);` because it is already done by
	 * the fmc-bus
	 */
	spec->fmc = NULL;
	spec->flags &= ~SPEC_FLAG_FMC_REGISTERED;
}
