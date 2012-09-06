/*
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 *
 * This work is part of the White Rabbit project, a research effort led
 * by CERN, the European Institute for Nuclear Research.
 */
#ifndef __LINUX_FMC_H__
#define __LINUX_FMC_H__
#include <linux/types.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>

struct fmc_device;
struct fmc_driver;

struct fmc_device_id {
	/* FIXME: the device ID must be defined according to eeprom contents */
	uint64_t unique_id;
};

#define FMC_MAX_CARDS 16 /* That many with the same matching driver... */

/* The driver is a pretty simple thing */
struct fmc_driver {
	struct device_driver driver;
	int (*probe)(struct fmc_device *);
	int (*remove)(struct fmc_device *);
	const struct fmc_device_id *id_table;
	/* What follows is for generic module parameters */
	int busid_n;
	int busid_val[FMC_MAX_CARDS];
	int gw_n;
	char *gw_val[FMC_MAX_CARDS];
};
#define to_fmc_driver(x) container_of((x), struct fmc_driver, driver)

/* These are the generic parameters, that drivers may instantiate */
#define FMC_PARAM_BUSID(_d) \
    module_param_array_named(busid, _d.busid_val, int, &_d.busid_n, 0444)
#define FMC_PARAM_GATEWARE(_d) \
    module_param_array_named(gateware, _d.gw_val, charp, &_d.gw_n, 0444)

/* To be carrier-independent, we need to abstract hardware access */
struct fmc_operations {
	uint32_t (*readl)(struct fmc_device *fmc, int offset);
	void (*writel)(struct fmc_device *fmc, uint32_t value, int offset);
	int (*validate)(struct fmc_device *fmc, struct fmc_driver *drv);
	int (*reprogram)(struct fmc_device *f, struct fmc_driver *d, char *gw);
	int (*irq_request)(struct fmc_device *fmc, irq_handler_t h,
			   char *name, int flags);
	void (*irq_ack)(struct fmc_device *fmc);
	int (*irq_free)(struct fmc_device *fmc);
	int (*read_ee)(struct fmc_device *fmc, int pos, void *d, int l);
	int (*write_ee)(struct fmc_device *fmc, int pos, const void *d, int l);
};

/* The device reports all information needed to access hw */
struct fmc_device {
	unsigned long flags;
	struct fmc_device_id id;	/* for the match function */
	struct fmc_operations *op;	/* carrier-provided */
	int irq;			/* according to host bus. 0 == none */
	int eeprom_len;			/* Usually 8kB, may be less */
	uint8_t *eeprom;		/* Full contents or leading part */
	char *carrier_name;		/* "SPEC" or similar, for special use */
	void *carrier_data;		/* "struct spec *" or equivalent */
	__iomem void *base;		/* May be NULL (Etherbone) */
	struct device dev;		/* For Linux use */
	struct device *hwdev;		/* The underlying hardware device */
	struct sdb_array *sdb;
	void *mezzanine_data;
};
#define to_fmc_device(x) container_of((x), struct fmc_device, dev)

#define FMC_DEVICE_HAS_GOLDEN		1
#define FMC_DEVICE_HAS_CUSTOM		2
#define FMC_DEVICE_NO_MEZZANINE		4

/* If the carrier offers no readl/writel, use base address */
static inline uint32_t fmc_readl(struct fmc_device *fmc, int offset)
{
	if (unlikely(fmc->op->readl))
		return fmc->op->readl(fmc, offset);
	return readl(fmc->base + offset);
}
static inline void fmc_writel(struct fmc_device *fmc, uint32_t val, int off)
{
	if (unlikely(fmc->op->writel))
		fmc->op->writel(fmc, val, off);
	else
		writel(val, fmc->base + off);
}

/* pci-like naming */
static inline void *fmc_get_drvdata(struct fmc_device *fmc)
{
	return dev_get_drvdata(&fmc->dev);
}

static inline void fmc_set_drvdata(struct fmc_device *fmc, void *data)
{
	dev_set_drvdata(&fmc->dev, data);
}

/* The 4 access points */
extern int fmc_driver_register(struct fmc_driver *drv);
extern void fmc_driver_unregister(struct fmc_driver *drv);
extern int fmc_device_register(struct fmc_device *tdev);
extern void fmc_device_unregister(struct fmc_device *tdev);

#endif /* __LINUX_FMC_H__ */
