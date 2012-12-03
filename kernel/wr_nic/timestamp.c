/*\
 * Timestamping routines for WR Switch
 *
 * Copyright (C) 2010 CERN (www.cern.ch)
 * Author: Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/sockios.h>
#include <linux/net_tstamp.h>

#include "wr-nic.h"

/* This checks if we already received the timestamp interrupt */
void wrn_tx_tstamp_skb(struct wrn_dev *wrn, int desc)
{
	struct skb_shared_hwtstamps *hwts;
	struct wrn_desc_pending	 *d = wrn->skb_desc + desc;
	struct sk_buff *skb = d->skb;
	struct timespec ts;
	u32 counter_ppsg; /* PPS generator nanosecond counter */
	u32 utc;

	if (!wrn->skb_desc[desc].valid)
		return;

	/* already reported by hardware: do the timestamping magic */
	wrn_ppsg_read_time(wrn, &counter_ppsg, &utc);

	/* We may be at the beginning og the next second */
	if(counter_ppsg < d->cycles)
		utc--;

	ts.tv_sec = (s32)utc & 0x7fffffff;
	ts.tv_nsec = d->cycles * NSEC_PER_TICK;
	if (! (d->valid & TS_INVALID)) {
		hwts = skb_hwtstamps(skb);
		hwts->hwtstamp = timespec_to_ktime(ts);
		skb_tstamp_tx(skb, hwts);
	}
	dev_kfree_skb_irq(skb);

	/* release both the descriptor and the tstamp entry */
	d->skb = 0;
	d->valid = 0;
}

/* This function, called by txtsu records the timestamp for the descriptor */
static int record_tstamp(struct wrn_dev *wrn, u32 tsval, u32 idreg, u32 r2)
{
	int frame_id = TXTSU_TSF_R1_FID_R(idreg);
	int ts_incorrect = r2 & TXTSU_TSF_R2_INCORRECT;
	struct skb_shared_hwtstamps *hwts;
	struct timespec ts;
	struct sk_buff *skb;
	u32 utc, counter_ppsg; /* PPS generator nanosecond counter */
	int i;

	/* Find the skb in the descriptor array */
	for (i = 0; i < WRN_NR_DESC; i++)
		if (wrn->skb_desc[i].skb
		    && wrn->skb_desc[i].frame_id == frame_id)
			break;

	if (i == WRN_NR_DESC) {
		/* Not found: Must be a PTP frame sent from the SPEC! */
		return 0;
	}

	skb = wrn->skb_desc[i].skb;

	wrn_ppsg_read_time(wrn, &counter_ppsg, &utc);

	if(counter_ppsg < (tsval & 0xfffffff))
		utc--;

	ts.tv_sec = (s32)utc & 0x7fffffff;
	ts.tv_nsec = (tsval & 0xfffffff) * NSEC_PER_TICK;

	/* Provide the timestamp  only if 100% sure about its correctness */
	if (!ts_incorrect) {
		hwts = skb_hwtstamps(skb);
		hwts->hwtstamp = timespec_to_ktime(ts);
		skb_tstamp_tx(skb, hwts);
	}
	dev_kfree_skb_irq(skb);
	wrn->skb_desc[i].skb = 0;
	return 0;
}

irqreturn_t wrn_tstamp_interrupt(int irq, void *dev_id)
{
	struct wrn_dev *wrn = dev_id;
	struct TXTSU_WB *regs = wrn->txtsu_regs;
	u32 r0, r1, r2;

	if (!regs)
		return IRQ_NONE; /* early interrupt? */

	/* printk("%s: %i\n", __func__, __LINE__); */
	/* FIXME: locking */
	r0 = readl(&regs->TSF_R0);
	r1 = readl(&regs->TSF_R1);
	r2 = readl(&regs->TSF_R2);

	record_tstamp(wrn, r0, r1, r2);
	writel(TXTSU_EIC_IER_NEMPTY, &wrn->txtsu_regs->EIC_ISR); /* ack irq */
	return IRQ_HANDLED;
}

int wrn_tstamp_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct wrn_ep *ep = netdev_priv(dev);
	struct hwtstamp_config config;

	if (copy_from_user(&config, rq->ifr_data, sizeof(config)))
		return -EFAULT;
	if (0) netdev_dbg(dev, "%s: tx type %i, rx filter %i\n", __func__,
		   config.tx_type, config.rx_filter);

	switch (config.tx_type) {
		/* Set up time stamping on transmission */
	case HWTSTAMP_TX_ON:
		set_bit(WRN_EP_STAMPING_TX, &ep->ep_flags);
		/* FIXME: enable timestamp on tx in hardware */
		break;

	case HWTSTAMP_TX_OFF:
		/* FIXME: disable timestamp on tx in hardware */
		clear_bit(WRN_EP_STAMPING_TX, &ep->ep_flags);
		break;

	default:
		return -ERANGE;
	}

	/*
	 * For the time being, make this really simple and stupid: either
	 * time-tag _all_ the incoming packets or none of them.
	 */
	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		/* FIXME: disable rx in hardware */
		clear_bit(WRN_EP_STAMPING_RX, &ep->ep_flags);
		break;

	default: /* All other case: activate stamping */
		/* FIXME: enable rx in hardware */
		set_bit(WRN_EP_STAMPING_RX, &ep->ep_flags);

		break;
	}

	/* FIXME: minic_update_ts_config(nic); */

	if (copy_to_user(rq->ifr_data, &config, sizeof(config)))
		return -EFAULT;
	return 0;
}

void wrn_tstamp_init(struct wrn_dev *wrn)
{
	/* enable TXTSU irq */
	writel(TXTSU_EIC_IER_NEMPTY, &wrn->txtsu_regs->EIC_IER);
}

