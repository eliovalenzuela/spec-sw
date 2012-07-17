/*
 * Copyright (C) 2010-2012 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 *
 * This work is part of the White Rabbit project, a research effort led
 * by CERN, the European Institute for Nuclear Research.
 */
#ifndef __SPEC_H__
#define __SPEC_H__
#include <linux/pci.h>
#include <linux/workqueue.h>
#include <linux/firmware.h>
#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/fmc.h>

#define PCI_VENDOR_ID_CERN	0x10dc
#define PCI_DEVICE_ID_SPEC		0x018d
#define PCI_VENDOR_ID_GENNUM	0x1a39
#define PCI_DEVICE_ID_GN4124		0x0004

#define SPEC_DEFAULT_LM32_ADDR 0x80000 /* used if "1" is passed */

/* Our device structure */
struct spec_dev {
	struct pci_dev		*pdev;
	struct resource		*area[3];	/* bar 0, 2, 4 */
	void			*remap[3];	/* ioremap of bar 0, 2, 4 */
	char			*submod_name;
	struct work_struct	work;
	const struct firmware	*fw;
	struct list_head	list;
	unsigned long		irqcount;
	atomic_t		has_submod;
	void			*sub_priv;
	struct fmc_device	*fmc;
};

/* Registers from the gennum header files */
enum {
	GNGPIO_BASE = 0xA00,
	GNGPIO_DIRECTION_MODE = GNGPIO_BASE + 0x4,
	GNGPIO_OUTPUT_ENABLE = GNGPIO_BASE + 0x8,
	GNGPIO_OUTPUT_VALUE = GNGPIO_BASE + 0xC,
	GNGPIO_INPUT_VALUE = GNGPIO_BASE + 0x10,

	FCL_BASE	= 0xB00,
	FCL_CTRL	= FCL_BASE,
	FCL_STATUS	= FCL_BASE + 0x4,
	FCL_IODATA_IN	= FCL_BASE + 0x8,
	FCL_IODATA_OUT	= FCL_BASE + 0xC,
	FCL_EN		= FCL_BASE + 0x10,
	FCL_TIMER_0	= FCL_BASE + 0x14,
	FCL_TIMER_1	= FCL_BASE + 0x18,
	FCL_CLK_DIV	= FCL_BASE + 0x1C,
	FCL_IRQ		= FCL_BASE + 0x20,
	FCL_TIMER_CTRL	= FCL_BASE + 0x24,
	FCL_IM		= FCL_BASE + 0x28,
	FCL_TIMER2_0	= FCL_BASE + 0x2C,
	FCL_TIMER2_1	= FCL_BASE + 0x30,
	FCL_DBG_STS	= FCL_BASE + 0x34,
	FCL_FIFO	= 0xE00,
	PCI_SYS_CFG_SYSTEM = 0x800
};

/* Functions in spec-fmc.c, used by spec-pci.c */
extern int spec_fmc_create(struct spec_dev *spec);
extern void spec_fmc_destroy(struct spec_dev *spec);

/* Function in spec-i2c.c, used by spec-fmc.c */
extern int spec_i2c_init(struct fmc_device *fmc);
extern void spec_i2c_exit(struct fmc_device *fmc);


#endif /* __SPEC_H__ */
