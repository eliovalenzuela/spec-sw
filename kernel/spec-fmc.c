/*
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 *
 * This work is part of the White Rabbit project, a research effort led
 * by CERN, the European Institute for Nuclear Research.
 */
#include <linux/slab.h>
#include <linux/fmc.h>
#include "spec.h"

int spec_fmc_create(struct spec_dev *spec)
{
	struct fmc_device *fmc;
	int ret;

	fmc = kzalloc(sizeof(*fmc), GFP_KERNEL);
	if (!fmc)
		return -ENOMEM;

	/* FIXME: many fields of the device are still NULL */
	fmc->carrier_name = "SPEC";
	fmc->carrier_data = spec;
	fmc->base = spec->remap[0];
	fmc->irq = spec->pdev->irq;

	ret = spec_i2c_init(fmc);
	if (ret) {
		kfree(fmc);
		return ret;
	}
	spec->fmc = fmc;
	ret = fmc_device_register(fmc);
	if (ret) {
		spec->fmc = NULL;
		kfree(fmc);
	}
	return ret;
}

void spec_fmc_destroy(struct spec_dev *spec)
{
	fmc_device_unregister(spec->fmc);
	spec_i2c_exit(spec->fmc);
	spec->fmc = NULL;
}
