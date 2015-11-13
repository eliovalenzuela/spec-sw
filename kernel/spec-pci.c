/*
 * Copyright (C) 2010-2012 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 *
 * This work is part of the White Rabbit project, a research effort led
 * by CERN, the European Institute for Nuclear Research.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/kmod.h>
#include <linux/sched.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <asm/unaligned.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>

#include "spec.h"
#include "loader-ll.h"

char *spec_fw_name = "fmc/spec-init.bin";
module_param_named(fw_name, spec_fw_name, charp, 0444);

int spec_use_msi = 0;
module_param_named(use_msi, spec_use_msi, int, 0444);

/* Load the FPGA. This bases on loader-ll.c, a kernel/user space thing */
int spec_load_fpga(struct spec_dev *spec, const void *data, int size)
{
	struct device *dev = &spec->pdev->dev;
	int i, wrote;
	unsigned long j;

	/* loader_low_level is designed to run from user space too */
	wrote = loader_low_level(0 /* unused fd */,
				 spec->remap[2], data, size);
	j = jiffies + 2 * HZ;
	/* Wait for DONE interrupt  */
	while(1) {
		udelay(100);
		i = readl(spec->remap[2] + FCL_IRQ);
		if (i & 0x8) {
			dev_info(dev, "FPGA programming successful\n");
			break;
		}

		if(i & 0x4) {
			dev_err(dev, "FPGA program error after %i writes\n",
				wrote);
			return -ETIMEDOUT;
		}

		if (time_after(jiffies, j)) {
			dev_err(dev, "FPGA timeout after %i writes\n", wrote);
			return -ETIMEDOUT;
		}
	}
	gpiofix_low_level(0 /* unused fd */, spec->remap[2]);
	loader_reset_fpga(0 /* unused fd */, spec->remap[2]);

	return 0;
}

int spec_load_fpga_file(struct spec_dev *spec, char *name)
{
	struct device *dev = &spec->pdev->dev;
	const struct firmware *fw;
	int err = 0;

	err = request_firmware(&fw, name, dev);
	if (err < 0) {
		dev_err(dev, "request firmware \"%s\": error %i\n", name, err);
		return err;
	}
	dev_info(dev, "got file \"%s\", %zi (0x%zx) bytes\n",
		 name, fw->size, fw->size);

	err = spec_load_fpga(spec, fw->data, fw->size);
	release_firmware(fw);
        return err;
}

static int spec_reconfigure(struct spec_dev *spec, struct fmc_gateware *gw)
{
	int ret;

	if (spec->flags & SPEC_FLAG_FMC_REGISTERED)
		spec_fmc_destroy(spec);

	/* Load the golden FPGA binary to read the eeprom */
	ret = spec_load_fpga_file(spec, spec_fw_name);
	if (ret)
		return ret;

	return spec_fmc_create(spec, gw);
}

/* * * * * * MISC DEVICE * * * * * */
static int spec_mdev_simple_open(struct inode *inode, struct file *file)
{
	struct miscdevice *mdev_ptr = file->private_data;

	file->private_data = container_of(mdev_ptr, struct spec_dev, mdev);

	return 0;
}

static ssize_t spec_mdev_write_raw(struct file *f, const char __user *buf,
				   size_t count, loff_t *offp)
{
	struct spec_dev *spec = f->private_data;
	struct fmc_gateware gw;
	int err = 0;

	if (!count)
		return -EINVAL;

	/* Copy FPGA bitstream to kernel space */
	gw.len = count;
	gw.bitstream = vmalloc(count);
	if (!gw.bitstream)
		return -ENOMEM;
	if (copy_from_user(gw.bitstream, buf, gw.len)) {
		err = -EFAULT;
		goto out;
	}

	dev_dbg(&spec->pdev->dev, "writing FPGA %p %ld (%zu + %lld)\n",
		gw.bitstream, gw.len, count, *offp);
	/* Program FPGA */
	err = spec_reconfigure(spec, &gw);
	if (err)
		dev_err(&spec->pdev->dev,
			"Manually program FPGA bitstream from buffer: fail\n");
	else
		dev_info(&spec->pdev->dev,
			 "Manually program FPGA bitstream from buffer: success\n");
out:
	vfree(gw.bitstream);
	return err ? err : count;
}

static const struct file_operations spec_fops = {
	.owner = THIS_MODULE,
	.open = spec_mdev_simple_open,
	.write  = spec_mdev_write_raw,
};

static int spec_create_misc_device(struct spec_dev *spec)
{
	spec->mdev.minor = MISC_DYNAMIC_MINOR;
	spec->mdev.fops = &spec_fops;
	spec->mdev.name = spec->name;

	return misc_register(&spec->mdev);
}

static void spec_destroy_misc_device(struct spec_dev *spec)
{
	misc_deregister(&spec->mdev);
}
/* * * * * * END MISC DEVICE * * * * */

static int spec_probe(struct pci_dev *pdev,
				const struct pci_device_id *id)
{
	struct spec_dev *spec;
	int i, ret;

	dev_info(&pdev->dev, " probe for device %04x:%04x\n",
		 pdev->bus->number, pdev->devfn);

	ret = pci_enable_device(pdev);
	if (ret < 0)
		return ret;
	 pci_set_master(pdev);

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	spec->pdev = pdev;

	if (spec_use_msi) {
		/*
		 * This should be "4" but arch/x86/kernel/apic/io_apic.c
		 * says "x86 doesn't support multiple MSI yet".
		 */
		#if LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0)
		ret = pci_enable_msi_block(pdev, 1);
		#else
		ret = pci_enable_msi_exact(pdev, 1);
		#endif
		if (ret < 0)
			dev_err(&pdev->dev, "%s: enable msi block: error %i\n",
				__func__, ret);
	}

	/* Remap our 3 bars */
	for (i = ret = 0; i < 3; i++) {
		struct resource *r = pdev->resource + (2 * i);
		if (!r->start)
			continue;
		spec->area[i] = r;
		if (r->flags & IORESOURCE_MEM) {
			spec->remap[i] = ioremap(r->start,
						r->end + 1 - r->start);
			if (!spec->remap[i])
				ret = -ENOMEM;
		}
	}
	if (ret)
		goto out_unmap;

	/* Put our 6 pins to a sane state (4 test points, 2 from FPGA) */
	gennum_mask_val(spec, 0xfc0, 0x000, GNGPIO_BYPASS_MODE); /* no AF */
	gennum_mask_val(spec, 0xfc0, 0xfc0, GNGPIO_DIRECTION_MODE); /* input */
	gennum_writel(spec, 0xffff, GNGPIO_INT_MASK_SET); /* disable */

	ret = spec_reconfigure(spec, NULL);
	if (ret)
		goto out_unmap;

	snprintf(spec->name, SPEC_NAME_LEN, "spec-%04x",
		 spec->pdev->bus->number << 8 | spec->pdev->devfn);

	/* Done */
	pci_set_drvdata(pdev, spec);

	ret = spec_create_misc_device(spec);
	if (ret) {
		dev_err(&spec->pdev->dev, "Error creating misc device\n");
		goto failed_misc;
	}

	return 0;

failed_misc:
	spec_fmc_destroy(spec);
out_unmap:
	for (i = 0; i < 3; i++) {
		if (spec->remap[i])
			iounmap(spec->remap[i]);
		spec->remap[i] = NULL;
		spec->area[i] = NULL;
	}
	pci_set_drvdata(pdev, NULL);
	if (spec_use_msi)
		pci_disable_msi(pdev);
	pci_disable_device(pdev);
	kfree(spec);
	return ret;
}

static void spec_remove(struct pci_dev *pdev)
{
	struct spec_dev *spec = pci_get_drvdata(pdev);
	int i;

	dev_info(&pdev->dev, "remove\n");

	spec_destroy_misc_device(spec);
	spec_fmc_destroy(spec);
	for (i = 0; i < 3; i++) {
		if (spec->remap[i])
			iounmap(spec->remap[i]);
		spec->remap[i] = NULL;
		spec->area[i] = NULL;
	}
	pci_set_drvdata(pdev, NULL);
	kfree(spec);
	//pci_disable_msi(pdev);
	pci_disable_device(pdev);

}


DEFINE_PCI_DEVICE_TABLE(spec_idtable) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CERN, PCI_DEVICE_ID_SPEC) },
	{ PCI_DEVICE(PCI_VENDOR_ID_GENNUM, PCI_DEVICE_ID_GN4124) },
	{ 0,},
};
MODULE_DEVICE_TABLE(pci, spec_idtable);

static struct pci_driver spec_driver = {
	.name = "spec",
	.id_table = spec_idtable,
	.probe = spec_probe,
	.remove = spec_remove,
};

static int __init spec_init(void)
{
	return pci_register_driver(&spec_driver);
}

static void __exit spec_exit(void)
{
	pci_unregister_driver(&spec_driver);

}

module_init(spec_init);
module_exit(spec_exit);

MODULE_VERSION(GIT_VERSION);
MODULE_LICENSE("GPL");

CERN_SUPER_MODULE;
