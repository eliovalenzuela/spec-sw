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
#include <linux/firmware.h>
#include <linux/fmc.h>
#include "wr-nic.h"
#include "spec.h"

static char *wrn_filename = "wr_nic_dio.bin";
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
	int ret;
	struct device *dev = fmc->hwdev;
	const struct firmware *fw;
	struct wrn_drvdata *dd;

	/* Driver data */
	dd = devm_kzalloc(&fmc->dev, sizeof(*dd), GFP_KERNEL);
	if (!dd)
		return -ENOMEM;
	fmc_set_drvdata(fmc, dd);

	/* We first write a new binary within the spec */
	if (wrn_filename) {

		ret = request_firmware(&fw, wrn_filename, dev);
		if (ret < 0) {
			dev_warn(dev, "request firmware \"%s\": error %i\n",
				wrn_filename, ret);
			goto out;
		}
		ret = fmc->op->reprogram(fmc, (void *)fw->data, fw->size);
		if (ret <0) {
			dev_err(dev, "write firmware \"%s\": error %i\n",
				wrn_filename, ret);
			goto out_fw;
		}
	}

	/* Verify that we have SDB at offset 0x30000 */
	if (fmc_readl(fmc, 0x30000) != 0x5344422d) {
		dev_err(dev, "Can't find SDB magic\n");
		ret = -ENODEV;
		goto out_fw;
	}

	/* Register the gpio stuff,  if we have kernel support */
	ret = wrn_gpio_init(fmc);
	if (ret < 0)
		goto out_fw;

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
out_fw:
	release_firmware(fw);
out:
	return ret;
}

int wrn_remove(struct fmc_device *fmc)
{
	fmc->op->irq_free(fmc);
	wrn_eth_exit(fmc);
	wrn_gpio_exit(fmc);
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
