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
#include <linux/ktime.h>
#include <asm/uaccess.h>
#include "spec-nic.h"
#include "wr_nic/wr-nic.h"
#include "wr-dio.h"

#define DIO_STAT

#ifdef DIO_STAT
#define wrn_stat 1
#else
#define wrn_stat 0
#endif

/* FIXME: should this access use fmc_readl/writel? */
static int wrn_dio_cmd_pulse(struct wrn_drvdata *drvdata,
			   struct wr_dio_cmd *cmd)
{
	struct DIO_WB __iomem *dio = drvdata->wrdio_base;
	struct PPSG_WB __iomem *ppsg = drvdata->ppsg_base;

	void __iomem *p;
	struct timespec *ts;
	uint32_t val;

	if (cmd->channel > 4)
		return -EINVAL; /* FIXME: mask */

	/* First, put this bit as output (FIXME: plain GPIO support?) */
	val = readl(&dio->OUT) | (1 << cmd->channel);
	writel(val, &dio->OUT);

	ts = cmd->t;

	/* if relative, add current second to timespec */
	if (cmd->flags & WR_DIO_F_REL) {
		uint32_t h1, l, h2;
		unsigned long now;

		h1 = readl(&ppsg->CNTR_UTCHI);
		l = readl(&ppsg->CNTR_UTCLO);
		h2 = readl(&ppsg->CNTR_UTCHI);
		if (h2 != h1)
			l = readl(&ppsg->CNTR_UTCLO);
		now = l;
		SET_HI32(now, h2);
		ts->tv_sec += now;
		printk("relative: %li -> %li\n", now, ts->tv_sec);
	}

	/* if not "now", set trig, trigh, cycles */
	if (!(cmd->flags & WR_DIO_F_NOW)) {
		/* not now: set relevant registers */
		p = &dio->TRIG0;
		p += (cmd->channel * 12);
		printk("%i -> %p\n", GET_HI32(ts->tv_sec), p + 4);
		writel(GET_HI32(ts->tv_sec), p + 4);
		printk("%li -> %p\n", ts->tv_sec, p);
		writel(ts->tv_sec, p);
		printk("%li -> %p\n", ts->tv_nsec / 8, p + 8);
		writel(ts->tv_nsec / 8, p + 8);
	}

	/* set the width */
	ts++;
	p = &dio->PROG0_PULSE;
	p += cmd->channel * 4;
	printk("%li -> %p\n", ts->tv_nsec / 8, p);
	writel(ts->tv_nsec / 8, p);

	/* no loop yet (FIXME: interrupts) */

	if (cmd->flags & WR_DIO_F_NOW)
		writel(1 << cmd->channel, &dio->PULSE);
	else
		writel(1 << cmd->channel, &dio->R_LATCH);

	return 0;

}

static int wrn_dio_cmd_stamp(struct wrn_drvdata *drvdata,
			     struct wr_dio_cmd *cmd)
{
	return -ENOTSUPP;
}


int wrn_mezzanine_ioctl(struct net_device *dev, struct ifreq *rq,
			       int ioctlcmd)
{
	struct wr_dio_cmd *cmd;
	struct wrn_drvdata *drvdata = dev->dev.parent->platform_data;
	ktime_t t, t0;
	int ret;

	if (ioctlcmd == PRIV_MEZZANINE_ID)
		return -EAGAIN; /* Special marker */
	if (ioctlcmd != PRIV_MEZZANINE_CMD)
		return -ENOIOCTLCMD;

	if (wrn_stat) {
		t0 = ktime_get();
	}

	/* The cmd struct can't fit in the stack, so allocate it */
	cmd = kmalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;
	ret = -EFAULT;
	if (copy_from_user(cmd, rq->ifr_data, sizeof(*cmd)))
		goto out;


	switch(cmd->command) {
	case WR_DIO_CMD_PULSE:
		ret = wrn_dio_cmd_pulse(drvdata, cmd);
		break;
	case WR_DIO_CMD_STAMP:
		ret = wrn_dio_cmd_stamp(drvdata, cmd);
		break;
	case WR_DIO_CMD_DAC:
	case WR_DIO_CMD_INOUT:
		ret = -ENOTSUPP;
		goto out;
	default:
		ret = -EINVAL;
		goto out;
	}

	if (copy_to_user(rq->ifr_data, cmd, sizeof(*cmd)))
		return -EFAULT;
out:
	kfree(cmd);

	if (wrn_stat) {
		t = ktime_sub(ktime_get(), t0);
		dev_info(&dev->dev, "ioctl: %li ns\n", (long)ktime_to_ns(t));
	}
	return ret;
}
