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

/*
 * This bus abstraction is developed separately from drivers, so we need
 * to check the version of the data structures we receive.
 */

#define FMC_MAJOR	1
#define FMC_MINOR	0
#define FMC_VERSION	((FMC_MAJOR << 16) | FMC_MINOR)
#define __FMC_MAJOR(x)	((x) >> 16)
#define __FMC_MINOR(x)	((x) & 0xffff)

struct fmc_device_id {
	/* FIXME: the device ID must be defined according to eeprom contents */
	uint64_t unique_id;
};

#define FMC_MAX_CARDS 16 /* That many with the same matching driver... */

/* The driver is a pretty simple thing */
struct fmc_driver {
	unsigned long version;
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

/*
 * Drivers may need to configure gpio pins in the carrier. To read input
 * (a very uncommon opeation, and definitely not in the hot paths), just
 * configure one gpio only and get 0 or 1 as retval of the config method
 */
struct fmc_gpio {
	char *carrier_name; /* name or NULL for virtual pins */
	int gpio;
	int _gpio;	/* internal use by the carrier */
	int mode;	/* GPIOF_DIR_OUT etc, from <linux/gpio.h> */
	int irqmode;	/* IRQF_TRIGGER_LOW and so on */
};

/* The numbering of gpio pins allows access to raw pins or virtual roles */
#define FMC_GPIO_RAW(x)		(x)		/* 4096 of them */
#define __FMC_GPIO_IS_RAW(x)	((x) < 0x1000)
#define FMC_GPIO_IRQ(x)		((x) + 0x1000)	/*  256 of them */
#define FMC_GPIO_LED(x)		((x) + 0x1100)	/*  256 of them */
#define FMC_GPIO_KEY(x)		((x) + 0x1200)	/*  256 of them */
#define FMC_GPIO_TP(x)		((x) + 0x1300)	/*  256 of them */
#define FMC_GPIO_USER(x)	((x) + 0x1400)	/*  256 of them */
/* We may add SCL and SDA, or other roles if the need arises */

/*
 * The operations are offered by each carrier and should make driver
 * design completely independent of th carrier. Named GPIO pins may be
 * the exception.
 */
struct fmc_operations {
	uint32_t (*readl)(struct fmc_device *fmc, int offset);
	void (*writel)(struct fmc_device *fmc, uint32_t value, int offset);
	int (*validate)(struct fmc_device *fmc, struct fmc_driver *drv);
	int (*reprogram)(struct fmc_device *f, struct fmc_driver *d, char *gw);
	int (*irq_request)(struct fmc_device *fmc, irq_handler_t h,
			   char *name, int flags);
	void (*irq_ack)(struct fmc_device *fmc);
	int (*irq_free)(struct fmc_device *fmc);
	int (*gpio_config)(struct fmc_device *fmc, struct fmc_gpio *gpio,
			   int ngpio);
	int (*read_ee)(struct fmc_device *fmc, int pos, void *d, int l);
	int (*write_ee)(struct fmc_device *fmc, int pos, const void *d, int l);
};

/* The device reports all information needed to access hw */
struct fmc_device {
	unsigned long version;
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
