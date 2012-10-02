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
#include <linux/platform_device.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/fmc.h>
#include <linux/fmc-sdb.h>
#include "spec-nic.h"
#include "wr_nic/wr-nic.h"

static struct fmc_driver wrn_drv;

static char *wrn_filename = WRN_GATEWARE_DEFAULT_NAME;
module_param_named(file, wrn_filename, charp, 0444);

static char *wrn_wrc_filename = WRN_WRC_DEFAULT_NAME;
module_param_named(wrc, wrn_wrc_filename, charp, 0444);

static int wrn_show_sdb;
module_param_named(show_sdb, wrn_show_sdb, int, 0444);


static int wrn_load_wrc(struct fmc_device *fmc, char *name,
			unsigned long ram, unsigned long ramsize)
{
	const struct firmware *fw;
	struct device *dev = fmc->hwdev;
	int ret, count;
	const uint32_t *p;

	ret = request_firmware(&fw, name, dev);
	if (ret < 0) {
		dev_err(dev, "request firmware \"%s\": error %i\n",
			name, ret);
		return ret;
	}
	if (fw->size > ramsize) {
		dev_err(dev, "firmware \"%s\" longer than ram (0x%lx)\n",
			name, ramsize);
		ret = -ENOMEM;
		goto out;
	}
	for (count = 0, p = (void *)fw->data; count < fw->size; count += 4, p++)
		fmc_writel(fmc, __cpu_to_be32(*p), ram + count);
out:
	release_firmware(fw);
	return ret;
}


int wrn_fmc_probe(struct fmc_device *fmc)
{
	int need_wrc = 0, ret = 0;
	struct device *dev = fmc->hwdev;
	struct wrn_drvdata *dd;
	signed long ram, syscon;
	unsigned long ramsize;
	char *filename;

	/* Driver data */
	dd = devm_kzalloc(&fmc->dev, sizeof(*dd), GFP_KERNEL);
	if (!dd)
		return -ENOMEM;
	fmc_set_drvdata(fmc, dd);

	/* We first write a new binary (and lm32) within the spec */
	ret = fmc->op->reprogram(fmc, &wrn_drv, wrn_filename);
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

	if ( (ret = fmc_scan_sdb_tree(fmc, WRN_SDB_ADDR)) < 0) {
		dev_err(dev, "scan fmc failed %i\n", ret);
		goto out;
	}
	if (wrn_show_sdb)
		fmc_show_sdb_tree(fmc);

	/*
	 * The gateware may not be including the WRC code, or the
	 * user may have asked for a specific file name. If so, load.
	 */
	ram = fmc_find_sdb_device(fmc->sdb, SDB_CERN, WRN_SDB_RAM, &ramsize);
	syscon = fmc_find_sdb_device(fmc->sdb, SDB_CERN, WRN_SDB_SYSCON, NULL);
	if (ram >= 0 && fmc_readl(fmc, ram) != 0x98000000)
		need_wrc = 1;
	filename = wrn_wrc_filename;
	if (strcmp(wrn_wrc_filename, WRN_WRC_DEFAULT_NAME)) {
		need_wrc = 1;
		/*
		 * If the user changed it, use the new name.
		 * But "1" means "do load the default"
		 */
		if (!strcmp(wrn_wrc_filename, "1"))
			filename = WRN_WRC_DEFAULT_NAME;
	}
	if (need_wrc && ((ram < 0) || (syscon < 0))) {
		dev_err(dev, "can't reprogram WRC: SDB failure\n");
		goto out;
	}
	if (need_wrc) {
		unsigned long j = jiffies + HZ/2;
		fmc_writel(fmc, 0x1deadbee, syscon);
		while ( !(fmc_readl(fmc, syscon) & (1 << 28)) )
			if (time_after(jiffies, j))
				break;
		if (time_after(jiffies, j)) {
			dev_err(dev, "can't reset LM32\n");
			fmc_writel(fmc, 0x0deadbee, syscon);
			goto out;
		}
		ret = wrn_load_wrc(fmc, filename, ram, ramsize);
		fmc_writel(fmc, 0x0deadbee, syscon);
		if (ret)
			goto out; /* message already reported */
		if (fmc_readl(fmc, ram) != 0x98000000)
			dev_warn(dev, "possible failure in wrc load\n");
		else
			dev_info(dev, "WRC program reloaded from \"%s\"\n",
				 filename);
	}
	/* After the LM32 started, give it time to set up */
	msleep(200);

	/* Register the gpio stuff,  if we have kernel support */
	ret = wrn_gpio_init(fmc);
	if (ret < 0)
		goto out;

	/* The network device */
	ret = wrn_eth_init(fmc);
	if (ret < 0)
		wrn_gpio_exit(fmc);
out:
	return ret;
}

int wrn_fmc_remove(struct fmc_device *fmc)
{
	wrn_eth_exit(fmc);
	wrn_gpio_exit(fmc);
	fmc_free_sdb_tree(fmc);
	return 0;
}

static struct fmc_driver wrn_fmc_drv = {
	.driver.name = KBUILD_MODNAME,
	.probe = wrn_fmc_probe,
	.remove = wrn_fmc_remove,
	/* no table, as the current match just matches everything */
};

static int wrn_init(void)
{
	int ret;

	ret = fmc_driver_register(&wrn_fmc_drv);
	if (ret < 0)
		return ret;
	platform_driver_register(&wrn_driver);
	if (ret < 0)
		fmc_driver_unregister(&wrn_fmc_drv);
	return ret;
}

static void wrn_exit(void)
{
	platform_driver_unregister(&wrn_driver);
	fmc_driver_unregister(&wrn_fmc_drv);
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
