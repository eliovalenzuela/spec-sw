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
#include <linux/string.h>
#include <linux/firmware.h>
#include <linux/init.h>
#include <linux/fmc.h>

/*
 * This module uses the firmware loader to program the whole or part
 * of the FMC eeprom. The meat is in the _run functions.  However, no
 * default file name is provided, to avoid accidental mishaps.
 */
static char *fwe_file;
module_param_named(file, fwe_file, charp, 444);

static int fwe_run_tlv(struct fmc_device *fmc, const struct firmware *fw)
{
	dev_err(fmc->hwdev, "not implemented yet\n");
	return -EOPNOTSUPP;
}

static int fwe_run_bin(struct fmc_device *fmc, const struct firmware *fw)
{
	int ret;

	dev_info(fmc->hwdev, "programming %i bytes\n", fw->size);
	ret = fmc->op->write_ee(fmc, 0, (void *)fw->data, fw->size);
	if (ret < 0) {
		dev_info(fmc->hwdev, "write_eeprom: error %i\n", ret);
		return ret;
	}
	return 0;
}

static int fwe_run(struct fmc_device *fmc, const struct firmware *fw)
{
	char *last4 = fwe_file + strlen(fwe_file) - 4;

	if (!strcmp(last4,".tlv"))
		return fwe_run_tlv(fmc, fw);
	if (!strcmp(last4,".bin"))
		return fwe_run_bin(fmc, fw);
	dev_err(fmc->hwdev, "invalid file name \"%s\"\n", fwe_file);
	return -EINVAL;
}

/*
 * Programming is done at probe time. Morever, if more than one FMC
 * card is probed for, only one is programmed. Unfortunately, it's
 * difficult to know in advance when probing the first card if others
 * are there.
 */
int fwe_probe(struct fmc_device *fmc)
{
	int err;
	static int done;
	const struct firmware *fw;
	struct device *dev = fmc->hwdev;

	if (!fwe_file) {
		dev_err(dev, "%s: no filename given: not programming\n",
			KBUILD_MODNAME);
		return -ENOENT;
	}
	if (done) {
		dev_err(dev, "%s: refusing to program another card\n",
			KBUILD_MODNAME);
		return -EAGAIN;
	}
	done++; /* we are starting with this board, don't do any more */
	err = request_firmware(&fw, fwe_file, dev);
	if (err < 0) {
		dev_err(dev, "request firmware \"%s\": error %i\n",
			fwe_file, err);
		return err;
	}
	fwe_run(fmc, fw);
	release_firmware(fw);
	return 0;
}

int fwe_remove(struct fmc_device *fmc)
{
	return 0;
}

static struct fmc_driver fwe_drv = {
	.driver.name = KBUILD_MODNAME,
	.probe = fwe_probe,
	.remove = fwe_remove,
	/* no table, as the current match just matches everything */
};

static int fwe_init(void)
{
	int ret;

	ret = fmc_driver_register(&fwe_drv);
	return ret;
}

static void fwe_exit(void)
{
	fmc_driver_unregister(&fwe_drv);
}

module_init(fwe_init);
module_exit(fwe_exit);

MODULE_LICENSE("GPL");
