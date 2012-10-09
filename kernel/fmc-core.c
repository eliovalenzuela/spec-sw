/*
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 *
 * This work is part of the White Rabbit project, a research effort led
 * by CERN, the European Institute for Nuclear Research.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/fmc.h>
#include "spec.h"

static int fmc_check_version(unsigned long version, const char *name)
{
	if (__FMC_MAJOR(version) != FMC_MAJOR) {
		pr_err("%s: \"%s\" has wrong major (has %li, expected %i)\n",
		       __func__, name, __FMC_MAJOR(version), FMC_MAJOR);
		return -EINVAL;
	}

	if (__FMC_MINOR(version) != FMC_MINOR)
		pr_info("%s: \"%s\" has wrong minor (has %li, expected %i)\n",
		       __func__, name, __FMC_MINOR(version), FMC_MINOR);
	return 0;
}

static int fmc_match(struct device *dev, struct device_driver *drv)
{
	//struct fmc_driver *fdrv = to_fmc_driver(drv);
	//struct fmc_device *fdev = to_fmc_device(dev);
	//const struct fmc_device_id *t = fdrv->id_table;

	/* Currently, return 1 every time, until we define policies */
	return 1;
}

static int fmc_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	//struct fmc_device *fdev = to_fmc_device(dev);

	/* FIXME: The MODALIAS */
	add_uevent_var(env, "MODALIAS=%s", "fmc");
	return 0;
}

static int fmc_probe(struct device *dev)
{
	struct fmc_driver *fdrv = to_fmc_driver(dev->driver);
	struct fmc_device *fdev = to_fmc_device(dev);

	return fdrv->probe(fdev);
}

static int fmc_remove(struct device *dev)
{
	struct fmc_driver *fdrv = to_fmc_driver(dev->driver);
	struct fmc_device *fdev = to_fmc_device(dev);

	return fdrv->remove(fdev);
}

static void fmc_shutdown(struct device *dev)
{
	/* not implemented but mandatory */
}

static struct bus_type fmc_bus_type = {
	.name = "fmc",
	.match = fmc_match,
	.uevent = fmc_uevent,
	.probe = fmc_probe,
	.remove = fmc_remove,
	.shutdown = fmc_shutdown,
};

/* Every device must have a release method: provide a default */
static void __fmc_release(struct device *dev){ }

/* This is needed as parent for our devices and dir in sysfs */
struct device fmc_bus = {
	.release = __fmc_release,
	.init_name = "fmc",
};

/* Functions for client modules */
int fmc_driver_register(struct fmc_driver *drv)
{
	if (fmc_check_version(drv->version, drv->driver.name))
		return -EINVAL;
	drv->driver.bus = &fmc_bus_type;
	return driver_register(&drv->driver);
}
EXPORT_SYMBOL(fmc_driver_register);

void fmc_driver_unregister(struct fmc_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL(fmc_driver_unregister);

int fmc_device_register(struct fmc_device *fdev)
{
	if (fmc_check_version(fdev->version, fdev->carrier_name))
		return -EINVAL;
	device_initialize(&fdev->dev);
	if (!fdev->dev.release)
		fdev->dev.release = __fmc_release;
	if (!fdev->dev.parent)
		fdev->dev.parent = &fmc_bus;
	fdev->dev.bus = &fmc_bus_type;
	{
		static int i;

		/* FIXME: the name */
		dev_set_name(&fdev->dev, "fmc-%04x", i++);
	}
	return device_add(&fdev->dev);
}
EXPORT_SYMBOL(fmc_device_register);

void fmc_device_unregister(struct fmc_device *fdev)
{
	device_del(&fdev->dev);
	put_device(&fdev->dev);
}
EXPORT_SYMBOL(fmc_device_unregister);

/* Init and exit are trivial */
static int fmc_init(void)
{
	int err;

	err = device_register(&fmc_bus);
	if (err)
		return err;
	err = bus_register(&fmc_bus_type);
	if (err)
		device_unregister(&fmc_bus);
	return err;
}

static void fmc_exit(void)
{
	bus_unregister(&fmc_bus_type);
	device_unregister(&fmc_bus);
}

module_init(fmc_init);
module_exit(fmc_exit);

MODULE_LICENSE("GPL");
