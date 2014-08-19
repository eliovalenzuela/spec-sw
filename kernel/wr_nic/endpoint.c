/*
 * Endoint-specific operations in the White-Rabbit switch network interface
 *
 * Copyright (C) 2010 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 * Partly from previous work by Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 * Partly from previous work by  Emilio G. Cota <cota@braap.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/io.h>
#include <linux/moduleparam.h>

#include "wr-nic.h"

static char *macaddr = "00:00:00:00:00:00";
module_param(macaddr, charp, 0444);

/* Copied from kernel 3.6 net/utils.c, it converts from MAC string to u8 array */
__weak int mac_pton(const char *s, u8 *mac)
{
	int i;

	/* XX:XX:XX:XX:XX:XX */
	if (strlen(s) < 3 * ETH_ALEN - 1)
		return 0;

	/* Don't dirty result unless string is valid MAC. */
	for (i = 0; i < ETH_ALEN; i++) {
		if (!strchr("0123456789abcdefABCDEF", s[i * 3]))
			return 0;
		if (!strchr("0123456789abcdefABCDEF", s[i * 3 + 1]))
			return 0;
		if (i != ETH_ALEN - 1 && s[i * 3 + 2] != ':')
			return 0;
	}
	for (i = 0; i < ETH_ALEN; i++) {
		mac[i] = (hex_to_bin(s[i * 3]) << 4) | hex_to_bin(s[i * 3 + 1]);
	}
	return 1;
}

/*
 * Phy access: used by link status, enable, calibration ioctl etc.
 * Called with endpoint lock (you'll lock the whole sequence of r/w)
 */
int wrn_phy_read(struct net_device *dev, int phy_id, int location)
{
	struct wrn_ep *ep = netdev_priv(dev);
	u32 val;

	if (WR_IS_NODE) {
		/*
		 * We cannot access the phy from Linux, because the phy
		 * is managed by the lm32 core. However, network manager
		 * insists on doing that, so we'd better not warn about it
		 */
		//WARN_ON(1); /* SPEC: no access */
		return -1;
	}

	wrn_ep_write(ep, MDIO_CR, EP_MDIO_CR_ADDR_W(location));
	while( (wrn_ep_read(ep, MDIO_ASR) & EP_MDIO_ASR_READY) == 0)
		;
	val = wrn_ep_read(ep, MDIO_ASR);
	/* mask from wbgen macros */
	return EP_MDIO_ASR_RDATA_R(val);
}

void wrn_phy_write(struct net_device *dev, int phy_id, int location,
		      int value)
{
	struct wrn_ep *ep = netdev_priv(dev);

	if (WR_IS_NODE) {
		/*
		 * We cannot access the phy from Linux, because the phy
		 * is managed by the lm32 core. However, network manager
		 * insists on doing that, so we'd better not warn about it
		 */
		//WARN_ON(1); /* SPEC: no access */
		return;
	}

	wrn_ep_write(ep, MDIO_CR,
		     EP_MDIO_CR_ADDR_W(location)
		     | EP_MDIO_CR_DATA_W(value)
		     | EP_MDIO_CR_RW);
	while( (wrn_ep_read(ep, MDIO_ASR) & EP_MDIO_ASR_READY) == 0)
		;
}

/* One link status poll per endpoint -- called with endpoint lock */
static void wrn_update_link_status(struct net_device *dev)
{
	struct wrn_ep *ep = netdev_priv(dev);
	u32 ecr, bmsr, bmcr, lpa;

	bmsr = wrn_phy_read(dev, 0, MII_BMSR);
	bmcr = wrn_phy_read(dev, 0, MII_BMCR);

	//netdev_dbg(dev, "%s: read %x %x", __func__, bmsr, bmcr);
//	printk("%s: read %x %x %x\n", __func__, bmsr, bmcr);

		/* Link wnt down? */
	if (!mii_link_ok(&ep->mii)) {
		if(netif_carrier_ok(dev)) {
			netif_carrier_off(dev);
			clear_bit(WRN_EP_UP, &ep->ep_flags);
			printk(KERN_INFO "%s: Link down.\n", dev->name);
			return;
		}
		return;
	}

	/* Currently the link is active */
	if(netif_carrier_ok(dev)) {
		/* Software already knows it's up */
		return;
	}

	/* What follows is the bring-up step */

	if (bmcr & BMCR_ANENABLE) { /* AutoNegotiation is enabled */
		if (!(bmsr & BMSR_ANEGCOMPLETE)) {
			/* Wait next timer, until it completes */
			return;
		}

		lpa  = wrn_phy_read(dev, 0, MII_LPA);

		if (0) { /* was commented in minic */
			wrn_ep_write(ep, FCR,
				     EP_FCR_TXPAUSE |EP_FCR_RXPAUSE
				     | EP_FCR_TX_THR_W(128)
				     | EP_FCR_TX_QUANTA_W(200));
		}

		printk(KERN_INFO "%s: Link up, lpa 0x%04x.\n",
		       dev->name, lpa);
	} else {
		/* No autonegotiation. It's up immediately */
		printk(KERN_INFO "%s: Link up.\n", dev->name);
	}
	netif_carrier_on(dev);
	set_bit(WRN_EP_UP, &ep->ep_flags);

	/* reset RMON counters */
	ecr = wrn_ep_read(ep, ECR);
	wrn_ep_write(ep, ECR, ecr | EP_ECR_RST_CNT);
	wrn_ep_write(ep, ECR, ecr );
}

/* Actual timer function. Takes the lock and calls above function */
static void wrn_ep_check_link(unsigned long dev_id)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct wrn_ep *ep = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&ep->lock, flags);
	wrn_update_link_status(dev);
	spin_unlock_irqrestore(&ep->lock, flags);

	mod_timer(&ep->ep_link_timer, jiffies + WRN_LINK_POLL_INTERVAL);
}

/* Endpoint open and close turn on and off the timer */
int wrn_ep_open(struct net_device *dev)
{
	struct wrn_ep *ep = netdev_priv(dev);
	unsigned long timerarg = (unsigned long)dev;
	int prio, prio_map;

	if (WR_IS_NODE) {
		netif_carrier_on(dev);
		return 0; /* No access to EP registers in the SPEC */
	}

	/* Prepare hardware registers: first config, then bring up */
	writel(0
	       | EP_VCR0_QMODE_W(0x3)		/* unqualified port */
	       | EP_VCR0_PRIO_VAL_W(4),		/* some mid priority */
		&ep->ep_regs->VCR0);

	/* Write default 802.1Q tag priority to traffic class mapping */
	prio_map = 0;
	for(prio=0; prio<8; ++prio) {
		prio_map |= (0x7 & prio) << (prio*3);
	}
	writel(prio_map, &ep->ep_regs->TCAR);


	/*
	 * enable RX timestamping (it has no impact on performance)
	 * and we need the RX OOB block to identify orginating endpoints
	 * for RXed packets -- Tom
	 */
	writel(EP_TSCR_EN_TXTS| EP_TSCR_EN_RXTS, &ep->ep_regs->TSCR);

	writel(0
	       | EP_ECR_PORTID_W(ep->ep_number)
	       | EP_ECR_RST_CNT
	       | EP_ECR_TX_EN
	       | EP_ECR_RX_EN,
		&ep->ep_regs->ECR);

	wrn_phy_write(dev, 0, MII_LPA, 0);
	wrn_phy_write(dev, 0, MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);

	/* Prepare the timer for link-up notifications */
	setup_timer(&ep->ep_link_timer, wrn_ep_check_link, timerarg);
	/* Not on spec. On spec this part of the function is never reached
	 * due to return in if(WR_IS_NODE) */
	mod_timer(&ep->ep_link_timer, jiffies + WRN_LINK_POLL_INTERVAL);
	return 0;
}

int wrn_ep_close(struct net_device *dev)
{
	struct wrn_ep *ep = netdev_priv(dev);

	if (WR_IS_NODE)
		return 0; /* No access to EP registers in the SPEC */
	/*
	 * Beware: the system loops in the del_timer_sync below if timer_setup
	 * had not been called either (see "if (WR_IS_NODE)" in ep_open above)
	 */

	writel(0, &ep->ep_regs->ECR);
	del_timer_sync(&ep->ep_link_timer);
	return 0;
}

/*
 * The probe functions brings up the endpoint and the logical ethernet
 * device within Linux. The actual network operations are in nic-core.c,
 * as they are not endpoint-specific, while the mii stuff is in this file.
 * The initial helper here shuts down everything, in case of failure or close
 */
static void __wrn_endpoint_shutdown(struct wrn_ep *ep)
{
	/* Not much to do it seems */
	writel(0, &ep->ep_regs->ECR); /* disable it all */
}

int wrn_endpoint_probe(struct net_device *dev)
{
	struct wrn_ep *ep = netdev_priv(dev);
	static u8 wraddr[6];
	int err;
	u32 val;

	if (is_zero_ether_addr(wraddr)) {
		err = mac_pton(macaddr, wraddr);
		if (!err)
			pr_err("wr_nic: probably invalid MAC address \"%s\".\n"
			       "Use format XX:XX:XX:XX:XX:XX\n", macaddr);
	}

	if (WR_IS_NODE) {
		/* If address is not provided as parameter read from lm32 */
		if (is_zero_ether_addr(wraddr)) {
			/* on the SPEC the lm32 already configured the mac address */
			val = readl(&ep->ep_regs->MACH);
			put_unaligned_be16(val, wraddr);
			val = readl(&ep->ep_regs->MACL);
			put_unaligned_be32(val, wraddr+2);
		}
	}

	if (WR_IS_SWITCH) {
		/* If the MAC address is 0, then randomize the first MAC */
		/* Do not randomize for SPEC */
		if (is_zero_ether_addr(wraddr)) {
			pr_warn("wr_nic: missing MAC address, randomize\n");
			/* randomize a MAC address, so lazy users can avoid ifconfig */
			random_ether_addr(wraddr);
			/* Clear the MSB on fourth octect to prevent bit overflow on OUI */
			wraddr[3] &= 0x7F;
		}
	}

	if (ep->ep_number == 0)
		pr_info("WR-nic: Using address %pM\n", wraddr);

	/* Use wraddr as MAC */
	memcpy(dev->dev_addr, wraddr, ETH_ALEN);
	pr_debug("wr_nic: assign MAC %pM to wr%d\n", dev->dev_addr, ep->ep_number);

	/* Check whether the ep has been sinthetized or not */
	val = readl(&ep->ep_regs->IDCODE);
	if (val != WRN_EP_MAGIC) {
		pr_info(KBUILD_MODNAME " EP%i (%s) has not been sintethized\n",
			ep->ep_number, dev->name);
		return -ENODEV;
	}
	/* Errors different from -ENODEV are fatal to insmod */

	dev_alloc_name(dev, "wr%d");
	wrn_netops_init(dev); /* function in ./nic-core.c */
	wrn_ethtool_init(dev); /* function in ./ethtool.c */
	/* Napi is not supported on this device */

	ep->mii.dev = dev;		/* Support for ethtool */
	ep->mii.mdio_read = wrn_phy_read;
	ep->mii.mdio_write = wrn_phy_write;
	ep->mii.phy_id = 0;
	ep->mii.phy_id_mask = 0x1f;
	ep->mii.reg_num_mask = 0x1f;

	ep->mii.force_media = 0;
	ep->mii.advertising = ADVERTISE_1000XFULL;
	ep->mii.full_duplex = 1;

	/* Finally, register and succeed, or fail and undo */
	err = register_netdev(dev);

	/* Increment MAC address for next endpoint */
	val = get_unaligned_be32(wraddr + 2);
	put_unaligned_be32(val + 1, wraddr + 2);

	if (err) {
		printk(KERN_ERR KBUILD_MODNAME ": Can't register dev %s\n",
		       dev->name);
		__wrn_endpoint_shutdown(ep);
		/* ENODEV means "no more" for the caller, so avoid it */
		return err == -ENODEV ? -EIO : err;
	}

	return 0;
}

/* Called for each endpoint, with a valid ep structure. The caller frees */
void wrn_endpoint_remove(struct net_device *dev)
{
	struct wrn_ep *ep = netdev_priv(dev);

	unregister_netdev(dev);
	__wrn_endpoint_shutdown(ep);
}
