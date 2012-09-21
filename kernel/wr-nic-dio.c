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
#include <linux/wait.h>
#include <linux/ktime.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/fmc.h>
#include <linux/fmc-sdb.h>
#include "spec-nic.h"
#include "wr_nic/wr-nic.h"
#include "wr-dio.h"

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

/* This is the structure we need to manage interrupts and loop internally */
#define WRN_DIO_BUFFER_LEN  512
struct dio_channel {
	struct timespec tsbuf[WRN_DIO_BUFFER_LEN];
	int bhead, btail;
	wait_queue_head_t q;
	int target_channel; /* -1 == none */
	struct timespec  delay;
};

struct dio_device {
	struct dio_channel ch[5];
};

static inline void wrn_ts_sub(struct timespec *ts, int nano)
{
	ts->tv_nsec -= nano;
	if (ts->tv_nsec < 0) {
		ts->tv_nsec += 1000 * 1000 * 1000;
		ts->tv_sec--;
	}
}

/* FIXME: should this access use fmc_readl/writel? */
static int wrn_dio_cmd_pulse(struct wrn_drvdata *drvdata,
			   struct wr_dio_cmd *cmd)
{
	struct DIO_WB __iomem *dio = drvdata->wrdio_base;
	void __iomem *base = dio;
	struct PPSG_WB __iomem *ppsg = drvdata->ppsg_base;
	struct regmap *map;
	struct timespec *ts;
	uint32_t reg;

	if (cmd->channel > 4)
		return -EINVAL; /* FIXME: mask */
	map = regmap + cmd->channel;

	/* First, put this bit as output (FIXME: plain GPIO support?) */
	reg = readl(&dio->OUT) | (1 << cmd->channel);
	writel(reg, &dio->OUT);

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
	}

	/* if not "now", set trig, trigh, cycles */
	if (!(cmd->flags & WR_DIO_F_NOW)) {
		/* Subtract 1 cycle, to count for output latencies */
		wrn_ts_sub(ts, 8);
		writel(ts->tv_nsec / 8, base + map->cycle);
		writel(GET_HI32(ts->tv_sec), base + map->trig_h);
		writel(ts->tv_sec, base + map->trig_l);
	}

	/* set the width */
	ts++;
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
	struct dio_device *d = drvdata->mezzanine_data;
	struct dio_channel *c;
	struct timespec *ts = cmd->t;
	struct regmap *map;
	int mask, ch, last;
	int nstamp = 0;

	if (cmd->flags & WR_DIO_F_MASK) {
		ch = 0;
		last = 4;
		mask = cmd->channel;
	} else {
		ch = cmd->channel;
		last = ch;
		mask = (1 << ch);
	}

	/* handle the 1-channel and mask case in the same loop */
	for (; ch <= last; ch++) {
		if (((1 << ch) & mask) == 0)
			continue;
		map = regmap + ch;
		c = d->ch + ch;
		while (1) {
			if (nstamp == WR_DIO_N_STAMP)
				break;
			if (c->bhead == c->btail)
				break;
			*ts = c->tsbuf[c->btail];
			c->btail = (c->btail + 1) % WRN_DIO_BUFFER_LEN;
			nstamp++;
			ts++;
		}
		if (nstamp) break;
	}
	cmd->nstamp = nstamp;
	if (!nstamp)
		return -EAGAIN;
	cmd->channel = ch; /* if any, they are all of this channel */
	return 0;
}

static int wrn_dio_cmd_inout(struct wrn_drvdata *drvdata,
			     struct wr_dio_cmd *cmd)
{
	struct DIO_WB __iomem *dio = drvdata->wrdio_base;
	struct wrn_gpio_block __iomem *gpio = drvdata->gpio_base;
	int mask, ch, last, bits;
	uint32_t reg;

	if (cmd->flags & WR_DIO_F_MASK) {
		ch = 0;
		last = 4;
		mask = cmd->channel;
	} else {
		ch = cmd->channel;
		last = ch;
		mask = (1 << ch);
		cmd->value <<= ch;
	}

	/* handle the 1-channel and mask case in the same loop */
	for (; ch <= last; ch++) {
		if (((1 << ch) & mask) == 0)
			continue;
		/* select the bits by shifting back the value field */
		bits = cmd->value >> ch;

		/* termination is bit 2 (0x4); register 0 clears, reg 4 sets */
		if (bits & WR_DIO_INOUT_TERM)
			writel(WRN_GPIO_TERM(ch), &gpio->set);
		else
			writel(WRN_GPIO_TERM(ch), &gpio->clear);

		reg = readl(&dio->OUT) & ~(1 << ch);
		if (bits & WR_DIO_INOUT_DIO) {
			writel(reg | (1 << ch), &dio->OUT);
			continue; /* if DIO, nothing more to do */
		}
		/* If not DIO, wait after we know if input or output */

		if (!(bits & WR_DIO_INOUT_OUTPUT)) {
			/* output-enable is low-active, so set bit 1 (0x2) */
			writel(WRN_GPIO_OE_N(ch), &gpio->set);
			writel(reg, &dio->OUT); /* not DIO */
			continue; /* input, no value to be set */
		}

		/* Output value is bit 0 (0x1) */
		if (bits & WR_DIO_INOUT_VALUE)
			writel(WRN_GPIO_VALUE(ch), &gpio->set);
		else
			writel(WRN_GPIO_VALUE(ch), &gpio->clear);
		/* Then clear the low-active output enable, bit 1 (0x2) */
		writel(WRN_GPIO_OE_N(ch), &gpio->clear);

		writel(reg, &dio->OUT); /* not DIO */
	}
	return 0;
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
	case WR_DIO_CMD_INOUT:
		ret = wrn_dio_cmd_inout(drvdata, cmd);
		break;
	case WR_DIO_CMD_DAC:
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

irqreturn_t wrn_dio_interrupt(struct fmc_device *fmc)
{
	struct platform_device *pdev = fmc->mezzanine_data;
	struct wrn_drvdata *drvdata = pdev->dev.platform_data;
	struct DIO_WB __iomem *dio = drvdata->wrdio_base;
	void __iomem *base = drvdata->wrdio_base;
	struct dio_device *d = drvdata->mezzanine_data;
	struct dio_channel *c;
	struct timespec *ts;
	struct regmap *map;
	uint32_t mask, reg;
	int ch, chm;

	if (unlikely(!fmc->eeprom)) {
		dev_err(fmc->hwdev, "No mezzanine: disabling interrupts\n");
		writel(0x1f, &dio->EIC_IDR);
		writel(0x1f, &dio->EIC_ISR);
		return IRQ_NONE;
	}

	mask = readl(&dio->EIC_ISR);

	/* Three indexes: channel, channel-mask, channel pointer */
	for (ch = 0, chm = 1, c = d->ch; mask; ch++, chm <<= 1, c++) {
		int h;

		if (!(mask & chm))
			continue;
		mask &= ~chm;

		/* Pull the FIFOs to the device structure */
		map = regmap + ch;
		h = c->bhead;
		while (1) {
			reg = readl(base + map->fifo_status);
			if (reg & 0x20000) /* empty */
				break;
			ts = c->tsbuf + h;
			c->bhead = (h + 1) % WRN_DIO_BUFFER_LEN;
			if (c->bhead == c->btail)
				c->btail = (c->btail + 1) % WRN_DIO_BUFFER_LEN;
			/*
			 * fifo is not-empty, pick one sample. Read
			 * cycles last, as that operation pops the FIFO
			 */
			ts->tv_sec = 0;
			SET_HI32(ts->tv_sec, readl(base + map->fifo_tai_h));
			ts->tv_sec |= readl(base + map->fifo_tai_l);
			ts->tv_nsec = 8 * readl(base + map->fifo_cycle);
			/* subtract 5 cycles lost in input sync circuits */
			wrn_ts_sub(ts, 40);
		}
		writel(chm, &dio->EIC_ISR); /* ack */

		if (h != c->bhead) {
			printk("ch %i, %i samples\n", c - d->ch, c->bhead - h);
			/* FIXME: if needed, perform the action */
		}
	}
	return IRQ_HANDLED;
}

/* Init and exit below are called when a netdevice is created/destroyed */
int wrn_mezzanine_init(struct net_device *dev)
{
	struct wrn_drvdata *drvdata = dev->dev.parent->platform_data;
	struct DIO_WB __iomem *dio = drvdata->wrdio_base;
	struct dio_device *d;
	int i;

	/* Allocate the data structure and enable interrupts for stamping */
	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;
	for (i = 0; i < ARRAY_SIZE(d->ch); i++)
		init_waitqueue_head(&d->ch[i].q);
	drvdata->mezzanine_data = d;

	/*
	 * Enable interrupts for FIFO, if there's no mezzanine the
	 * handler will notice and disable the interrupts
	 */
	writel(0x1f, &dio->EIC_IER);
	return 0;
}

void wrn_mezzanine_exit(struct net_device *dev)
{
	struct wrn_drvdata *drvdata = dev->dev.parent->platform_data;
	struct DIO_WB __iomem *dio = drvdata->wrdio_base;

	writel(0x1f, &dio->EIC_IDR);
	if (drvdata->mezzanine_data)
		kfree(drvdata->mezzanine_data);
}

