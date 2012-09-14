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

/* We need a clear mapping for the registers of the various bits */
struct regmap {
	int trig_l;
	int trig_h;
	int cycle;
	int pulse;
	int fifo_tai_l;
	int fifo_tai_h;
	int fifo_cycle;
	int fifo_status;
};

#define R(x) (offsetof(struct DIO_WB, x))
static struct regmap regmap[] = {
	{
		.trig_l = R(TRIG0),
		.trig_h = R(TRIGH0),
		.cycle = R(CYC0),
		.pulse = R(PROG0_PULSE),
		.fifo_tai_l = R(TSF0_R0),
		.fifo_tai_h = R(TSF0_R1),
		.fifo_cycle = R(TSF0_R2),
		.fifo_status = R(TSF0_CSR),
	}, {
		.trig_l = R(TRIG1),
		.trig_h = R(TRIGH1),
		.cycle = R(CYC1),
		.pulse = R(PROG1_PULSE),
		.fifo_tai_l = R(TSF1_R0),
		.fifo_tai_h = R(TSF1_R1),
		.fifo_cycle = R(TSF1_R2),
		.fifo_status = R(TSF1_CSR),
	}, {
		.trig_l = R(TRIG2),
		.trig_h = R(TRIGH2),
		.cycle = R(CYC2),
		.pulse = R(PROG2_PULSE),
		.fifo_tai_l = R(TSF2_R0),
		.fifo_tai_h = R(TSF2_R1),
		.fifo_cycle = R(TSF2_R2),
		.fifo_status = R(TSF2_CSR),
	}, {
		.trig_l = R(TRIG3),
		.trig_h = R(TRIGH3),
		.cycle = R(CYC3),
		.pulse = R(PROG3_PULSE),
		.fifo_tai_l = R(TSF3_R0),
		.fifo_tai_h = R(TSF3_R1),
		.fifo_cycle = R(TSF3_R2),
		.fifo_status = R(TSF3_CSR),
	}, {
		.trig_l = R(TRIG4),
		.trig_h = R(TRIGH4),
		.cycle = R(CYC4),
		.pulse = R(PROG4_PULSE),
		.fifo_tai_l = R(TSF4_R0),
		.fifo_tai_h = R(TSF4_R1),
		.fifo_cycle = R(TSF4_R2),
		.fifo_status = R(TSF4_CSR),
	}
};

/* FIXME: should this access use fmc_readl/writel? */
static int wrn_dio_cmd_pulse(struct wrn_drvdata *drvdata,
			   struct wr_dio_cmd *cmd)
{
	struct DIO_WB __iomem *dio = drvdata->wrdio_base;
	void __iomem *base = dio;
	struct PPSG_WB __iomem *ppsg = drvdata->ppsg_base;
	struct regmap *map;
	struct timespec *ts;
	uint32_t val;

	if (cmd->channel > 4)
		return -EINVAL; /* FIXME: mask */
	map = regmap + cmd->channel;

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
		printk("%x %x %x\n", map->trig_h, map->trig_l, map->cycle);
		writel(GET_HI32(ts->tv_sec), base + map->trig_h);
		writel(ts->tv_sec, base + map->trig_l);
		writel(ts->tv_nsec / 8, base + map->cycle);
	}

	/* set the width */
	ts++;
	printk("%x\n", map->pulse);
	writel(ts->tv_nsec / 8, base + map->pulse);

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
