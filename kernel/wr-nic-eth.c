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
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/fmc.h>
#include <linux/fmc-sdb.h>
#include "spec-nic.h"
#include "wr_nic/wr-nic.h"
#include "spec.h"

/*
 * nic-device.c defines a platform driver. We need to allocate
 * a platform device each time this init function is called
 * (part of the code is from wr_nic/module.c).
 */

static void wrn_release(struct device *dev)
{
	/* nothing to do, but mandatory function */
	pr_debug("%s\n", __func__);
}

static struct platform_device wrn_pdev = {
	/* other fields filled after it's copied: see wrn_eth_init() */
	.name = KBUILD_MODNAME,
	.dev.release = &wrn_release,
};

irqreturn_t wrn_handler(int irq, void *dev_id)
{
	struct fmc_device *fmc = dev_id;

	fmc->op->irq_ack(fmc);
	printk("%s: irq %i\n", __func__, irq);
	return IRQ_HANDLED;
}

static struct wrn_core {
	const char *name;
	uint64_t vendor;
	uint32_t device;
} wrn_cores[] = {
	[WRN_FB_NIC]  = {"NIC",		SDB_CERN,	WRN_SDB_NIC},
	[WRN_FB_EP]   = {"Endpoint",	SDB_CERN,	WRN_SDB_EP},
	[WRN_FB_PPSG] = {"PPS-Gen",	SDB_CERN,	WRN_SDB_PPSG},
	[WRN_FB_TS]   = {"Tx-Stamp",	SDB_CERN,	WRN_SDB_TS},
};

int wrn_eth_init(struct fmc_device *fmc)
{
	struct device *dev = fmc->hwdev;
	struct resource *resarr;
	struct platform_device *pdev;
	struct wrn_drvdata *drvdata;
	struct wrn_dev *wrn;
	struct wrn_core *c;
	int i, ret;

	ret = fmc->op->irq_request(fmc, wrn_handler, "wr-nic", 0);
	if (ret < 0) {
		dev_err(dev, "Can't request interrupt\n");
		return ret;
	}

	/* Make a copy of the platform device and register it */
	ret = -ENOMEM;
	pdev = kmemdup(&wrn_pdev, sizeof(wrn_pdev), GFP_KERNEL);
	resarr = kzalloc(sizeof(*resarr) * ARRAY_SIZE(wrn_cores), GFP_KERNEL);
	drvdata = kzalloc(sizeof(*drvdata), GFP_KERNEL);
	wrn =  kzalloc(sizeof(*wrn), GFP_KERNEL);

	if (!pdev || !resarr || !drvdata || !wrn)
		goto out_mem;

	for (i = 0, c = wrn_cores; i < ARRAY_SIZE(wrn_cores); i++, c++) {
		signed long start;
		unsigned long size;

		start = fmc_find_sdb_device(fmc->sdb, c->vendor, c->device,
					    &size);
		if (start < 0) {
			dev_err(dev, "Can't find sdb core \"%s\"\n", c->name);
			goto out_mem;
		}
		dev_info(dev, "core \"%s\": offset %08lx\n", c->name, start);
		resarr[i].name = c->name;
		resarr[i].flags = IORESOURCE_MEM;
		resarr[i].start = (unsigned long)fmc->base + start;
		resarr[i].end = resarr[i].start + size - 1;
	}

	pdev->resource = resarr;
	pdev->num_resources = ARRAY_SIZE(wrn_cores);
	drvdata->wrn = wrn;
	drvdata->fmc = fmc;
	/* FIXME: drvdata->gpio_base etc */
	pdev->dev.platform_data = drvdata;
	fmc->mezzanine_data = pdev;
	platform_device_register(pdev);

	wrn_pdev.id++; /* for the next one */
	return 0;

out_mem:
	kfree(wrn);
	kfree(drvdata);
	kfree(resarr);
	kfree(pdev);
	fmc->op->irq_free(fmc);
	return ret;
}

void wrn_eth_exit(struct fmc_device *fmc)
{
	struct platform_device *pdev = fmc->mezzanine_data;
	struct wrn_drvdata *drvdata = pdev->dev.platform_data;

	platform_device_unregister(pdev);
	kfree(drvdata->wrn);
	kfree(drvdata);
	kfree(pdev->resource);
	kfree(pdev);
	fmc->mezzanine_data = NULL;
	fmc->op->irq_free(fmc);
}
