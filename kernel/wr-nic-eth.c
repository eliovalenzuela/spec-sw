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

int wrn_eth_init(struct fmc_device *fmc)
{
	return 0;
}

void wrn_eth_exit(struct fmc_device *fmc)
{
}
