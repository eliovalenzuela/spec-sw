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
#include <linux/fmc.h>
#include <linux/fmc-sdb.h>
#include "spec-nic.h"
#include "wr_nic/wr-nic.h"
#include "wr-dio.h"

int wrn_mezzanine_ioctl(struct net_device *dev, struct ifreq *rq,
			       int cmd)
{
	return -EAGAIN;
}
