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
#include <linux/fmc.h>
#include <linux/fmc-sdb.h>
#include "wr-nic.h"
#include "spec.h"

static char *wrn_filename = WRN_GATEWARE_DEFAULT_NAME;
module_param_named(file, wrn_filename, charp, 0444);

irqreturn_t wrn_handler(int irq, void *dev_id)
{
	struct fmc_device *fmc = dev_id;

	fmc->op->irq_ack(fmc);
	printk("%s: irq %i\n", __func__, irq);
	return IRQ_HANDLED;
}

int wrn_probe(struct fmc_device *fmc)
{
	int ret = 0;
	struct device *dev = fmc->hwdev;
	struct wrn_drvdata *dd;

	/* Driver data */
	dd = devm_kzalloc(&fmc->dev, sizeof(*dd), GFP_KERNEL);
	if (!dd)
		return -ENOMEM;
	fmc_set_drvdata(fmc, dd);

	/* We first write a new binary (and lm32) within the spec */
	ret = fmc->op->reprogram(fmc, WRN_GATEWARE_DEFAULT_NAME);
	if (ret <0) {
		dev_err(dev, "write firmware \"%s\": error %i\n",
			wrn_filename, ret);
		goto out;
	}

	/* Verify that we have SDB at offset 0x63000 */
	if (fmc_readl(fmc, 0x63000) != 0x5344422d) {
		dev_err(dev, "Can't find SDB magic\n");
		ret = -ENODEV;
		goto out;
	}
	dev_info(dev, "Gateware successfully loaded\n");

	if ( (ret = fmc_scan_sdb_tree(fmc, 0x63000)) < 0) {
		dev_err(dev, "scan fmc failed %i\n", ret);
		goto out;
	}
	fmc_show_sdb_tree(fmc);

	/* FIXME: load lm32 */

	/* Register the gpio stuff,  if we have kernel support */
	ret = wrn_gpio_init(fmc);
	if (ret < 0)
		goto out;

	/* The netword device */
	ret = wrn_eth_init(fmc);
	if (ret < 0)
		goto out_gpio;

	/* The interrupt */
	ret = fmc->op->irq_request(fmc, wrn_handler, "wr-nic", 0);
	if (ret < 0) {
		dev_err(dev, "Can't request interrupt\n");
		goto out_nic;
	}
	return 0;

out_nic:
	wrn_eth_exit(fmc);
out_gpio:
	wrn_gpio_exit(fmc);
out:
	return ret;
}

int wrn_remove(struct fmc_device *fmc)
{
	fmc->op->irq_free(fmc);
	wrn_eth_exit(fmc);
	wrn_gpio_exit(fmc);
	fmc_free_sdb_tree(fmc);
	return 0;
}

static struct fmc_driver wrn_drv = {
	.driver.name = KBUILD_MODNAME,
	.probe = wrn_probe,
	.remove = wrn_remove,
	/* no table, as the current match just matches everything */
};

static int wrn_init(void)
{
	int ret;

	ret = fmc_driver_register(&wrn_drv);
	return ret;
}

static void wrn_exit(void)
{
	fmc_driver_unregister(&wrn_drv);
}

module_init(wrn_init);
module_exit(wrn_exit);

/* If no gpio lib is there, this weak applies */
int __weak wrn_gpio_init(struct fmc_device *fmc)
{
	return 0;
}
void __weak wrn_gpio_exit(struct fmc_device *fmc)
{
}

MODULE_LICENSE("GPL");
