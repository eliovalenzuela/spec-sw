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
#include "spec.h"
#include "spec-nic.h"
#include "wr-dio.h"
#include "wr_nic/wr-nic.h"
#include "wbgen-regs/vic-regs.h"

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

#define WRN_ALL_MASK (WRN_VIC_MASK_NIC | WRN_VIC_MASK_TXTSU | WRN_VIC_MASK_DIO)

/* This is the interrupt handler, that uses the VIC to know which is which */
irqreturn_t wrn_handler(int irq, void *dev_id)
{
	struct fmc_device *fmc = dev_id;
	struct platform_device *pdev = fmc->mezzanine_data;
	struct wrn_drvdata *drvdata;
	struct VIC_WB *vic;
	uint32_t mask;
	irqreturn_t ret = IRQ_HANDLED;

	if (!pdev) {
		/* too early, just do nothing */
		printk("%s: irq %i\n", __func__, irq);
		fmc->op->irq_ack(fmc);
		return ret;
	}

	drvdata = pdev->dev.platform_data;
	vic = (typeof(vic))drvdata->vic_base;

	while ( (mask = readl(&vic->RISR)) ) {
		if (mask & WRN_VIC_MASK_NIC)
			ret = wrn_interrupt(irq, drvdata->wrn);
		if (mask & WRN_VIC_MASK_TXTSU)
			ret = wrn_tstamp_interrupt(irq, drvdata->wrn);
		if (mask & WRN_VIC_MASK_DIO)
			ret = wrn_dio_interrupt(fmc /* different arg! */);
		writel(mask, &vic->EOIR);
	}

	fmc->op->irq_ack(fmc);

	/*
	 * The VIC is really level-active, but my Gennum refuses to work
	 * properly on level interrupts (maybe it's just me). So I'll use
	 * the typical trick you see in level-irq Ethernet drivers when
	 * they are plugged to edge-irq gpio lines: force an edge in case
	 * the line has become active again while we were serving it.
	 * (with a big thank you to Tomasz and Grzegorz for the sequence)
	 *
	 * Actually, we should check the input line before doing this...
	 */
	writel(WRN_ALL_MASK, &vic->IDR); /* disable sources */
	writel(0xff, &vic->EOIR);
	udelay(5);
	writel(WRN_ALL_MASK, &vic->IER); /* enable sources again */

	return ret;
}

static int wrn_vic_init(struct fmc_device *fmc)
{
	struct platform_device *pdev = fmc->mezzanine_data;
	struct wrn_drvdata *drvdata = pdev->dev.platform_data;
	struct VIC_WB *vic = (typeof(vic))drvdata->vic_base;

	writel(VIC_CTL_ENABLE | VIC_CTL_POL, &vic->CTL);
	writel(WRN_ALL_MASK, &vic->IER);
	return 0;
}

static void wrn_vic_exit(struct fmc_device *fmc)
{
	struct platform_device *pdev = fmc->mezzanine_data;
	struct wrn_drvdata *drvdata = pdev->dev.platform_data;
	struct VIC_WB *vic = (typeof(vic))drvdata->vic_base;

	writel(0xff, &vic->IDR);
	writel(0, &vic->CTL);
}

struct wrn_core {
	const char *name;
	uint64_t vendor;
	uint32_t device;
	int offset;
};

/* These cores are used by the nic driver itself */
static struct wrn_core wrn_cores[] = {
	[WRN_FB_NIC]  = {"NIC",		SDB_CERN,	WRN_SDB_NIC},
	[WRN_FB_EP]   = {"Endpoint",	SDB_CERN,	WRN_SDB_EP},
	[WRN_FB_PPSG] = {"PPS-Gen",	SDB_CERN,	WRN_SDB_PPSG},
	[WRN_FB_TS]   = {"Tx-Stamp",	SDB_CERN,	WRN_SDB_TS},
};

/* These cores are used by the wr-nic fmc device */
static struct wrn_core wrn_cores2[] = {
	{
		"VIC",		SDB_CERN,	WRN_SDB_VIC,
		offsetof(struct wrn_drvdata, vic_base)
	},{
		"GPIO",		SDB_CERN,	WRN_SDB_GPIO,
		offsetof(struct wrn_drvdata, gpio_base)
	},{
		"WR-DIO",	SDB_7SOL,	WRN_SDB_WRDIO,
		offsetof(struct wrn_drvdata, wrdio_base)
	},{
		"PPS-Gen",	SDB_CERN,	WRN_SDB_PPSG,
		offsetof(struct wrn_drvdata, ppsg_base)
	}
};

int wrn_eth_init(struct fmc_device *fmc)
{
	struct device *dev = fmc->hwdev;
	struct resource *resarr;
	struct platform_device *pdev;
	struct wrn_drvdata *drvdata;
	struct wrn_dev *wrn;
	struct wrn_core *c;
	struct spec_dev *spec = fmc->carrier_data;
	signed long start;
	unsigned long size;
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

		start = fmc_find_sdb_device(fmc->sdb, c->vendor, c->device,
					    &size);
		if (start < 0) {
			dev_err(dev, "Can't find sdb core \"%s\"\n", c->name);
			goto out_mem;
		}
		resarr[i].name = c->name;
		resarr[i].flags = IORESOURCE_MEM;
		/* FIXME: spec-specific for the io area to be remapped */
		resarr[i].start = spec->area[0]->start + start;
		resarr[i].end = resarr[i].start + size - 1;
		resarr[i].parent = spec->area[0];
	}
	for (i = 0, c = wrn_cores2; i < ARRAY_SIZE(wrn_cores2); i++, c++) {
		start = fmc_find_sdb_device(fmc->sdb, c->vendor, c->device,
					    &size);
		if (start < 0) {
			dev_err(dev, "Can't find sdb core \"%s\"\n", c->name);
			continue;
		}
		/* use c->offset to copy and already-remapped value */
		*((void **)((u8 *)drvdata + c->offset)) = fmc->base + start;
	}
	printk("%p %p %p\n", drvdata->vic_base, drvdata->gpio_base,
	       drvdata->wrdio_base);
	pdev->resource = resarr;
	pdev->num_resources = ARRAY_SIZE(wrn_cores);
	drvdata->wrn = wrn;
	drvdata->fmc = fmc;
	pdev->dev.platform_data = drvdata;
	fmc->mezzanine_data = pdev;
	platform_device_register(pdev);
	wrn_vic_init(fmc);

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
	struct wrn_drvdata *drvdata;

	wrn_vic_exit(fmc);
	if (pdev)
		platform_device_unregister(pdev);
	if (pdev) {
		drvdata = pdev->dev.platform_data;
		kfree(drvdata->wrn);
		kfree(drvdata);
		kfree(pdev->resource);
		kfree(pdev);
	}
	fmc->mezzanine_data = NULL;
	fmc->op->irq_free(fmc);
}
