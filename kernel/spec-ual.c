/*
 * Copyright (C) 2010-2012 CERN (www.cern.ch)
 * Author: Federico Vaga <federico.vaga@cern.ch>
 * License : GPL version 2 or later
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fmc.h>
#include <linux/fmc-sdb.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include <ual.h>
#include "spec.h"
#include "gncore-dma.h"
#include "loader-ll.h"

void spec_ual_sdb_info(struct spec_dev *spec)
{
	int err;

	pr_info("%s:%d\n", __func__, __LINE__);
	fmc_free_sdb_tree(spec->fmc);
	pr_info("%s:%d\n", __func__, __LINE__);
	err = fmc_scan_sdb_tree(spec->fmc, 0);
	pr_info("%s:%d\n", __func__, __LINE__);
	if (err) {
		dev_err(&spec->pdev->dev, "Cannot scan SDB: err %d\n", err);
	        return;
	}
	fmc_show_sdb_tree(spec->fmc);

	pr_info("%s:%d\n", __func__, __LINE__);
	if (!spec->ual)
		return;

	/* DMA irq base address - to avoid conflicts with FPGA driver */
	spec->ual->irq_dma_base_addr = fmc_find_sdb_device(spec->fmc->sdb,
							   0xce42,
							   0xd5735ab4,
							   NULL);
	spec->priv_dma = gncore_dma_init(spec->fmc);
	if (IS_ERR(spec->priv_dma)) {
		dev_err(&spec->pdev->dev,
			"GNCORE DMA component is not part of the bitstream\n");
		spec->priv_dma = NULL;
	}
	spec->ual->tree = spec->fmc->sdb;
	pr_info("%s:%d %p\n", __func__, __LINE__, spec, spec->priv_dma);
}

static int spec_ual_create(struct ual *ual)
{
	struct spec_dev *spec = ual->priv;

	spec_ual_sdb_info(spec);

	return 0;
}

static void spec_ual_destroy(struct ual *ual)
{
	struct spec_dev *spec = ual->priv;

	if (spec->priv_dma)
		gncore_dma_exit(spec->priv_dma);
}

static int spec_ual_irq_request(struct ual *ual, int src, irq_handler_t h,
				     char *name, int flags)
{
	struct spec_dev *spec = ual->priv;
	struct fmc_device *fmc = spec->fmc;

	if (!(flags & IRQF_SHARED))
		fmc->irq = src;      /* VIC */

	return fmc->op->irq_request(fmc, h, name, flags);
}

static void spec_ual_irq_free(struct ual *ual, int src)
{
	struct spec_dev *spec = ual->priv;
	struct fmc_device *fmc = spec->fmc;

	if (src)
		fmc->irq = src; /* VIC */
	fmc->op->irq_free(fmc);
}

static void spec_ual_irq_ack(struct ual *ual)
{
	struct spec_dev *spec = ual->priv;
	struct fmc_device *fmc = spec->fmc;

	fmc->op->irq_ack(fmc);
}

static int spec_ual_irq_is_managed(struct ual *ual, int src)
{
	struct spec_dev *spec = ual->priv;

	return vic_is_managed(spec->vic, src);
}



static int spec_fmc_reload(struct spec_dev *spec)
{
	int err;

	/* Remove previous FMC device */
	spec_fmc_destroy(spec);

	/* Load the golden firmware to allow FMC initialization */
	err = spec_load_fpga_file(spec, spec_fw_name);
	if (err)
		return err;
	dev_info(&spec->pdev->dev, "%s\n", __func__);
	/* Create a new FMC device */
        return spec_fmc_create(spec);
}

static int spec_ual_load_fw_file(struct ual *ual, char *file)
{
	struct spec_dev *spec = ual->priv;
	int err;

	/* Load a new set of FMC devices */
	err = spec_fmc_reload(spec);
	if (err)
		return err;

	/* Load the new firmware */
	err = spec_load_fpga_file(spec, file);
	if (err)
		return err;

	/* Update SDB */
	spec_ual_sdb_info(spec);

	return 0;
}

static int spec_ual_load_fw_raw(struct ual *ual, void *buf, size_t len)
{
	struct spec_dev *spec = ual->priv;
	int err;

	/* Load a new set of FMC devices */
	err = spec_fmc_reload(spec);
	if (err)
		goto out;

	/* Program FPGA */
	dev_info(&spec->pdev->dev, "%s %p %zu\n", __func__, buf, len);
	err = spec_load_fpga(spec, buf, len);
	if (err)
		goto out;

	/* Update SDB */
	spec_ual_sdb_info(spec);

out:
	return 0;
}


static uint32_t spec_ual_readl(struct ual *ual, unsigned long addr)
{
	struct spec_dev *spec = ual->priv;
	struct fmc_device *fmc = spec->fmc;

	return fmc_readl(fmc, addr);
}

static void spec_ual_writel(struct ual *ual, uint32_t val, unsigned long addr)
{
	struct spec_dev *spec = ual->priv;
	struct fmc_device *fmc = spec->fmc;

	fmc_writel(fmc, val, addr);
}

static int spec_ual_trans_is_running(struct ual *ual)
{
	struct spec_dev *spec = ual->priv;

	if (unlikely(!spec->priv_dma)) {
		dev_err(&spec->pdev->dev, "GNCORE component not loaded\n");
		return -EPERM;
	}

	return gncore_dma_is_running(spec->priv_dma);
}

/**
 * FIXME we should not do this because it breaks the UAL independency
 */
static void spec_ual_trans_over(void *data, uint32_t status)
{
	struct fmc_device *fmc = data;
	struct spec_dev *spec = fmc->carrier_data;
	struct ual *ual = spec->ual;

	wake_up_interruptible(&ual->q_dma);
}

static void spec_ual_trans_run(struct ual *ual,
				 void (*trans_over)(void *data,
						      uint32_t status))
{
	struct spec_dev *spec = ual->priv;

	/* FIXME We are not using trans_over, but we should*/

	if (unlikely(!spec->priv_dma)) {
		dev_err(&spec->pdev->dev, "GNCORE component not loaded\n");
		return;
	}

	/* Run DMA transfer */
	gncore_dma_run(spec->priv_dma, ual->buffer, ual->len, ual->offset,
		       ual->direction, spec_ual_trans_over);
}

struct ual_op spec_ual_op = {
	.create = spec_ual_create,
	.destroy = spec_ual_destroy,
	.readl = spec_ual_readl,
	.writel = spec_ual_writel,
	.irq_request = spec_ual_irq_request,
	.irq_free = spec_ual_irq_free,
	.irq_ack = spec_ual_irq_ack,
	.irq_is_managed = spec_ual_irq_is_managed,
	.load_fw_file = spec_ual_load_fw_file,
	.load_fw_raw = spec_ual_load_fw_raw,
	.trans_is_running = spec_ual_trans_is_running,
	.trans_run = spec_ual_trans_run,
};
