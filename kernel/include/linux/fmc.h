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
	uint32_t (*writel)(struct fmc_device *d, int offset, uint32_t value);
	void (*irq_ack)(struct fmc_device *d);
};

/* The device reports all information needed to access hw */
struct fmc_device {
	struct fmc_device_id id;
	struct fmc_operations *op;
	int irq;
	int eeprom_len;
	uint8_t *eeprom;
	char *carrier_name;
	void *carrier_data;
	__iomem void *base;
	struct device dev;
};
#define to_fmc_device(x) container_of((x), struct fmc_device, dev)

extern int fmc_driver_register(struct fmc_driver *drv);
extern void fmc_driver_unregister(struct fmc_driver *drv);
extern int fmc_device_register(struct fmc_device *tdev);
extern void fmc_device_unregister(struct fmc_device *tdev);

#endif /* __LINUX_FMC_H__ */
