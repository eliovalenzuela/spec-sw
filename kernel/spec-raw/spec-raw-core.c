/*
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Federico Vaga <federico.vaga@cern.ch>
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 *
 * This work is part of the White Rabbit project, a research effort led
 * by CERN, the European Institute for Nuclear Research.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/fmc.h>
#include <linux/fmc-sdb.h>
#include <linux/dma-mapping.h>
#include <linux/debugfs.h>

#include "spec-raw.h"

struct dentry *root_dir;
struct list_head sr_devices;
static DEFINE_SPINLOCK(sr_lock);

static struct fmc_driver sr_drv;
FMC_PARAM_BUSID(sr_drv);
FMC_PARAM_GATEWARE(sr_drv);

static int sr_show_sdb;
module_param_named(show_sdb, sr_show_sdb, int, 0444);
MODULE_PARM_DESC(show_sdb, "Print a dump of the gateware's SDB tree.");

/* Device part .. */
static int sr_probe(struct fmc_device *fmc);
static int sr_remove(struct fmc_device *fmc);

static struct fmc_driver sr_drv = {
	.version = FMC_VERSION,
	.driver.name = KBUILD_MODNAME,
	.probe = sr_probe,
	.remove = sr_remove,
	/* no table: we want to match everything */
};

static void sr_getcomponents(struct sr_instance *sr)
{
	int err;

	/* Try to get SDB information (if there) */
	err = fmc_scan_sdb_tree(sr->fmc, 0);
	if (err < 0) {
		dev_warn(&sr->fmc->dev, "%s: no SDB in the bitstream.\n",
			KBUILD_MODNAME);
		return ;
	}

	/* Look for the VIC IRQ controller */
	sr->vic_base_addr = fmc_find_sdb_device(sr->fmc->sdb,
						0xce42, 0x13, NULL);
	/* Look for the DMA engine */
	sr->dma_base_addr = fmc_find_sdb_device(sr->fmc->sdb,
						0xce42, 0x601, NULL);
	/* Look for the IRQ controller */
	sr->irq_dma_base_addr = fmc_find_sdb_device(sr->fmc->sdb,
						    0xce42, 0xd5735ab4, NULL);

	if (sr_show_sdb)
		fmc_show_sdb_tree(sr->fmc);
}

static int sr_register_misc(struct miscdevice *misc, const struct file_operations *fops,
			    const char *devname, const char *type)
{
	int ret;

	misc->minor = MISC_DYNAMIC_MINOR;
	misc->fops = fops;
	misc->name = kasprintf(GFP_KERNEL, "%s-%s", devname, type);
	if (!misc->name)
		return -ENOMEM;

	ret = misc_register(misc);
	if (ret < 0) {
		kfree(misc->name);
		spin_unlock(&sr_lock);
		return ret;
	}
	pr_info("Created misc device \"%s\"\n", misc->name);

	return 0;
}

static void sr_unregister_misc(struct miscdevice *misc)
{
	misc_deregister(misc);
	kfree(misc->name);
}

/* probe and remove must allocate and release a misc device */
static int sr_probe(struct fmc_device *fmc)
{
	struct ual_irq_status *st;
	int ret, i;
	int index = 0;
	char *fwname;
	struct sr_instance *sr;

	if (fmc->op->validate)
		index = fmc->op->validate(fmc, &sr_drv);
	if (index < 0)
		return -EINVAL; /* not our device: invalid */

	/* Check if the carrier is a SPEC */
	if (strcmp(fmc->carrier_name, "SPEC")) {
		pr_err("spec-raw: works only with SPEC carrier\n");
		return -EINVAL;
	}

	if (sr_drv.gw_n)
		fwname = "";	/* use the gateware provided by the user */
	else
		fwname = NULL;	/* use the default SPEC gateware */

	dev_info(fmc->hwdev, "Gateware (%s)\n", fwname);
	/* We first write a new binary (and lm32) within the carrier */
	ret = fmc->op->reprogram(fmc, &sr_drv, fwname);
	if (ret) {
		dev_err(fmc->hwdev, "write firmware \"%s\": error %i\n",
				fwname, ret);
		return ret;
	}
	dev_info(fmc->hwdev, "Gateware successfully loaded\n");

	if (dma_set_mask(fmc->hwdev, DMA_BIT_MASK(64))) {
		dev_warn(&fmc->dev, "62-bit DMA addressing not available\n");
		/* Check if hardware supports 32-bit DMA */
		if (dma_set_mask(fmc->hwdev, DMA_BIT_MASK(32))) {
			dev_err(&fmc->dev,
				"32-bit DMA addressing not available\n");
			return -EINVAL;
		}
	}

	/* Create a char device: we want to create it anew */
	sr = kzalloc(sizeof(*sr), GFP_KERNEL);
	if (!sr)
		return -ENOMEM;
	sr->fmc = fmc;
	fmc_set_drvdata(sr->fmc, sr);

	sr_getcomponents(sr);

	spin_lock(&sr_lock);
	ret = sr_register_misc(&sr->ual.m_reg, &sr_reg_fops,
			       dev_name(&sr->fmc->dev), "reg");
	if (ret) {
		spin_unlock(&sr_lock);
		goto err_misc_reg;
	}
	dev_set_drvdata(sr->ual.m_reg.this_device, sr);

	ret = sr_register_misc(&sr->ual.m_dma, &sr_dma_fops,
			       dev_name(&sr->fmc->dev), "dma");
	if (ret) {
		spin_unlock(&sr_lock);
		goto err_misc_dma;
	}
	dev_set_drvdata(sr->ual.m_dma.this_device, sr);

	ret = sr_register_misc(&sr->ual.m_irq, &sr_irq_fops,
			       dev_name(&sr->fmc->dev), "irq");
	if (ret) {
		spin_unlock(&sr_lock);
		goto err_misc_irq;
	}
	dev_set_drvdata(sr->ual.m_irq.this_device, sr);
	list_add(&sr->list, &sr_devices);
	spin_unlock(&sr_lock);

	sr->ual.dma_buf = NULL;

	mutex_init(&sr->mtx);
	spin_lock_init(&sr->lock);
	spin_lock_init(&sr->ual.irq_lock);
	init_waitqueue_head(&sr->q_dma);
	init_waitqueue_head(&sr->ual.q_irq);

	sr->ual.r_idx_irq = 0;
	sr->ual.w_idx_irq = 0;
	/* Initialize subscription list to invalid source/status */
	for (i = 0; i < UAL_IRQ_MAX_SUBSCRIPTION; ++i) {
		st = &sr->ual.subscribed_irqs[i];
		st->source = 0xBADC0FFE;
		st->status = 0x0;
		st->offset = 0x0;
	}

	ret = sr_irq_create_sysfs(sr);
	if (ret < 0)
		goto err_sysfs;

	/* Request for the VIC if it is there */
	ret = sr_request_irqs(sr);
	if (ret < 0)
		goto err_req;

	/* Create a debug directory */
	root_dir = debugfs_create_dir(sr->ual.m_reg.name, NULL);
	if (!IS_ERR_OR_NULL(sr->dbg_dir)) {
		sr->dbg_dir = debugfs_create_dir(sr->ual.m_reg.name, root_dir);
		if (!IS_ERR_OR_NULL(sr->dbg_dir)) {
			sr->dma_loopback = debugfs_create_file("test_dma_loopback",
						0444, sr->dbg_dir, sr,
						&sr_dbgfs_dma_loop_op);
			sr->dma_write_seq = debugfs_create_file("write_seq",
						0222, sr->dbg_dir, sr,
						&sr_dbgfs_dma_write_seq);
			sr->dma_write_zero = debugfs_create_file("write_zero",
						0222, sr->dbg_dir, sr,
						&sr_dbgfs_dma_write_zero);
		}
	} else {
		root_dir = NULL;
		dev_warn(&sr->fmc->dev, "Cannot create debugfs\n");
	}

	dev_info(&sr->fmc->dev, "%s successfully loaded\n", KBUILD_MODNAME);

	return 0;
err_req:
	sr_irq_remove_sysfs(sr);
err_sysfs:
	sr_unregister_misc(&sr->ual.m_irq);
err_misc_irq:
	sr_unregister_misc(&sr->ual.m_dma);
err_misc_dma:
	sr_unregister_misc(&sr->ual.m_reg);
err_misc_reg:
	kfree(sr);
	return ret;
}

static int sr_remove(struct fmc_device *fmc)
{
	struct sr_instance *sr;

	list_for_each_entry(sr, &sr_devices, list)
		if (sr->fmc == fmc)
			break;
	if (sr->fmc != fmc) {
		dev_err(&fmc->dev, "remove called but not found\n");
		return -ENODEV;
	}
	sr_irq_remove_sysfs(sr);
	sr_unregister_misc(&sr->ual.m_irq);
	sr_unregister_misc(&sr->ual.m_reg);
	sr_unregister_misc(&sr->ual.m_dma);
	if (root_dir)
		debugfs_remove_recursive(root_dir);
	if (sr->ual.dma_buf)
		vfree(sr->ual.dma_buf);
	sr_free_irqs(sr);
	spin_lock(&sr_lock);
	list_del(&sr->list);
	kfree(sr);
	spin_unlock(&sr_lock);

	return 0;
}


static int sr_init(void)
{
	int ret;

	INIT_LIST_HEAD(&sr_devices);
	ret = fmc_driver_register(&sr_drv);
	return ret;
}

static void sr_exit(void)
{
	fmc_driver_unregister(&sr_drv);
}

module_init(sr_init);
module_exit(sr_exit);

MODULE_AUTHOR("Federico Vaga <federico.vaga@cern.ch>");
MODULE_DESCRIPTION("General Purpose Driver for SPEC");
MODULE_VERSION(GIT_VERSION);
MODULE_LICENSE("GPL");

CERN_SUPER_MODULE;
