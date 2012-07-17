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
#include <linux/fmc.h>
#include <asm/unaligned.h>

#include "spec.h"
#include "loader-ll.h"

static char *spec_fw_name = "fmc/spec-init.bin";
module_param_named(fw_name, spec_fw_name, charp, 0444);

/* Load the FPGA. This bases on loader-ll.c, a kernel/user space thing */
static int spec_load_fpga(struct spec_dev *spec)
{
	const struct firmware *fw;
	struct device *dev = &spec->pdev->dev;
	unsigned long j;
	int i, err = 0, wrote;
	char *name = spec_fw_name; /* FIXME: temporary hack */

	err = request_firmware(&fw, name, dev);
	if (err < 0)
		return err;
	dev_info(dev, "got file \"%s\", %i (0x%x) bytes\n",
		 spec_fw_name, fw->size, fw->size);

	/* loader_low_level is designed to run from user space too */
	wrote = loader_low_level(0 /* unused fd */,
				 spec->remap[2], fw->data, fw->size);
	j = jiffies + 2 * HZ;
	/* Wait for DONE interrupt  */
	while(1) {
		udelay(100);
		i = readl(spec->remap[2] + FCL_IRQ);
		if (i & 0x8) {
			dev_info(dev, "FPGA programming sucessful\n");
			goto out;
		}

		if(i & 0x4) {
			dev_err(dev, "FPGA program error after %i writes\n",
				wrote);
			err = -ETIMEDOUT;
			goto out;
		}

		if (time_after(jiffies, j)) {
			dev_err(dev, "FPGA timeout after %i writes\n", wrote);
			err = -ETIMEDOUT;
			goto out;
		}
	}
out:
	release_firmware(fw);
        return err;
}

static int spec_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct spec_dev *spec;
	int i, ret;

	dev_info(&pdev->dev, " probe for device %04x:%04x\n",
		 pdev->bus->number, pdev->devfn);

	ret = pci_enable_device(pdev);
	if (ret < 0)
		return ret;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	spec->pdev = pdev;

	if ( (i = pci_enable_msi_block(pdev, 1)) < 0)
		pr_err("%s: enable ms block: %i\n", __func__, i);

	/* Remap our 3 bars */
	for (i = 0; i < 3; i++) {
		struct resource *r = pdev->resource + (2 * i);
		if (!r->start)
			continue;
		spec->area[i] = r;
		if (r->flags & IORESOURCE_MEM)
			spec->remap[i] = ioremap(r->start,
						r->end + 1 - r->start);
	}

	pci_set_drvdata(pdev, spec);

	/* Load the golden FPGA binary to read the eeprom */
	spec_load_fpga(spec);

	/* FIXME: eeprom */

	/* Done */
	return 0;
}

static void spec_remove(struct pci_dev *pdev)
{
	struct spec_dev *spec = pci_get_drvdata(pdev);
	int i;

	dev_info(&pdev->dev, "remove\n");

	for (i = 0; i < 3; i++) {
		iounmap(spec->remap[i]);
		spec->remap[i] = NULL;
		spec->area[i] = NULL;
	}
	pci_set_drvdata(pdev, NULL);
	kfree(spec);
	pci_disable_msi(pdev);
	pci_disable_device(pdev);

}


DEFINE_PCI_DEVICE_TABLE(spec_idtable) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CERN, PCI_DEVICE_ID_SPEC) },
	{ PCI_DEVICE(PCI_VENDOR_ID_GENNUM, PCI_DEVICE_ID_GN4124) },
	{ 0,},
};

static struct pci_driver spec_driver = {
	.name = "spec",
	.id_table = spec_idtable,
	.probe = spec_probe,
	.remove = spec_remove,
};

static int spec_init(void)
{
	INIT_LIST_HEAD(&spec_list);
	return pci_register_driver(&spec_driver);
}

static void spec_exit(void)
{
	pci_unregister_driver(&spec_driver);

}

module_init(spec_init);
module_exit(spec_exit);

MODULE_LICENSE("GPL");
