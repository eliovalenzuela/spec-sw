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
	uint32_t vector;
	irqreturn_t ret = IRQ_HANDLED;

	if (!pdev) {
		/* too early, just do nothing */
		dev_info(fmc->hwdev, "early irq %i, ignoring\n", irq);
		fmc->op->irq_ack(fmc);
		return ret;
	}

	drvdata = pdev->dev.platform_data;
	vic = (typeof(vic)) drvdata->vic_base;

	/*
	 * VIC operation algorithm:
	 *   - when a peripheral interrupt arrives (as seen in RISR register),
	 *     it is masked by IMR and latched in bit of ISR register:
	 *
	 *     ISR |= (RISR & IMR);
	 *     if (ISR != 0) {
	 *         int current_irq = priority_decode(ISR)
	 *         VAR = IVT_RAM[current_irq];
	 *         MASTER_IRQ = CTL.POL;
	 *
	 *         wait (write to EOIR)
	 *
	 *         if (CTL.EMU_ENA)
	 *             pulse(MASTER_IRQ, CTL.EMU_LEN, !CTL.POL)
	 *     } else {
	 *         MASTER_IRQ = !CTL.POL
	 *     }   
	 *
	 * The VIC was inspired by the original VIC used in NXP's ARM MCUs.
	 */

	/* read pending vector address - the index of currently pending IRQ. */
	vector = readl(&vic->VAR);

	if (vector == WRN_VIC_ID_NIC)
		ret = wrn_interrupt(irq, drvdata->wrn);
	else if (vector == WRN_VIC_ID_TXTSU)
		ret = wrn_tstamp_interrupt(irq, drvdata->wrn);
	else if (vector == WRN_VIC_ID_DIO)
		ret = wrn_dio_interrupt(fmc /* different arg! */);

	fmc->op->irq_ack(fmc);

	writel(0, &vic->EOIR);

	return ret;
}

static int wrn_vic_init(struct fmc_device *fmc)
{
	struct platform_device *pdev = fmc->mezzanine_data;
	struct wrn_drvdata *drvdata = pdev->dev.platform_data;
	struct VIC_WB *vic = (typeof(vic))drvdata->vic_base;

	/* fill the vector table */
	writel(WRN_VIC_ID_TXTSU, &vic->IVT_RAM[WRN_VIC_ID_TXTSU]);
	writel(WRN_VIC_ID_NIC,   &vic->IVT_RAM[WRN_VIC_ID_NIC]);
	writel(WRN_VIC_ID_DIO,   &vic->IVT_RAM[WRN_VIC_ID_DIO]);

	/* 4us edge emulation timer (counts in 16ns steps) */
	writel(VIC_CTL_ENABLE | VIC_CTL_POL | VIC_CTL_EMU_EDGE | \
	       VIC_CTL_EMU_LEN_W(4000 / 16), &vic->CTL);

	writel(WRN_ALL_MASK, &vic->IER);
	return 0;
}

static void wrn_vic_exit(struct fmc_device *fmc)
{
	struct platform_device *pdev = fmc->mezzanine_data;
	struct wrn_drvdata *drvdata = pdev->dev.platform_data;
	struct VIC_WB *vic = (typeof(vic))drvdata->vic_base;

	writel(0xff, &vic->IDR);

	/* Interrupt line is !POL when EN == 0, so leave polarity bit on */
	writel(VIC_CTL_POL, &vic->CTL);
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

struct fmc_gpio wrn_gpio_cfg[] = {
	{
		.gpio = FMC_GPIO_IRQ(1),
		.mode = GPIOF_DIR_IN,
		.irqmode = IRQF_TRIGGER_RISING,
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

	ret = fmc->op->irq_request(fmc, wrn_handler, "wr-nic", IRQF_SHARED);
	if (ret < 0) {
		dev_err(dev, "Can't request interrupt\n");
		return ret;
	}
	/* FIXME: we should request irq0, self-test and then move to irq1 */
	fmc->op->gpio_config(fmc, wrn_gpio_cfg, ARRAY_SIZE(wrn_gpio_cfg));

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
		*((void **)((u8 *)drvdata + c->offset)) =
			fmc->fpga_base + start;
	}
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
