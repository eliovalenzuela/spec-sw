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
#include <linux/device.h>

struct fmc_device;
struct fmc_driver;

struct fmc_device_id {
	/* FIXME: the device ID must be defined according to eeprom contents */
	uint64_t unique_id;
};

/* The driver is a pretty simple thing */
struct fmc_driver {
	struct device_driver driver;
	int (*probe)(struct fmc_device *);
	int (*remove)(struct fmc_device *);
	const struct fmc_device_id *id_table;

};
#define to_fmc_driver(x) container_of((x), struct fmc_driver, driver)

/* To be carrier-independent, we need to abstract hardware access */
struct fmc_operations {
	uint32_t (*readl)(struct fmc_device *d, int offset);
	void (*writel)(struct fmc_device *d, int offset, uint32_t value);
	int (*reprogram)(struct fmc_device *d, void *data, int len);
	void (*irq_ack)(struct fmc_device *d);
	int (*read_ee)(struct fmc_device *d, int pos, void *data, int len);
	int (*write_ee)(struct fmc_device *d, int pos, void *data, int len);
};

/* The device reports all information needed to access hw */
struct fmc_device {
	struct fmc_device_id id;	/* for the match function */
	struct fmc_operations *op;	/* carrier-provided */
	int irq;			/* according to host bus. 0 == none */
	int eeprom_len;			/* Usually 8kB, may be less */
	uint8_t *eeprom;		/* Full contents or leading part */
	char *carrier_name;		/* "SPEC" or similar, for special use */
	void *carrier_data;		/* "struct spec *" or equivalent */
	__iomem void *base;		/* May be NULL (Etherbone) */
	struct device dev;		/* For Linux use */
};
#define to_fmc_device(x) container_of((x), struct fmc_device, dev)

extern int fmc_driver_register(struct fmc_driver *drv);
extern void fmc_driver_unregister(struct fmc_driver *drv);
extern int fmc_device_register(struct fmc_device *tdev);
extern void fmc_device_unregister(struct fmc_device *tdev);

#endif /* __LINUX_FMC_H__ */
