// SPDX-License-Identifier: GPLv2
/*
 * Copyright (C) 2017 CERN (www.cern.ch)
 * Author: Federico Vaga <federico.vaga@cern.ch>
 */

#ifndef __SPEC_COMPAT_H__
#define __SPEC_COMPAT_H__
#include <linux/gpio.h>
#include <linux/version.h>

static inline struct device *gc_to_dev(struct gpio_chip *gc)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,5,0)
	return gc->dev;
#else
	return gc->parent;
#endif
}

static inline void gc_assign_dev(struct gpio_chip *gc,
				 struct device *dev)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,5,0)
	gc->dev = &fmc->dev;
#else
	gc->parent = &fmc->dev;
#endif
}

static inline void gpiochip_remove_compat(struct gpio_chip *gc)
{
#if LINUX_VERSION_CODE > KERNEL_VERSION(3,17,0)
	gpiochip_remove(gc);
#else
	int ret;

	ret = gpiochip_remove(gc);
	if (ret)
		dev_err(gc_to_dev(gc),
			"DANGER %i! gpio chip can't be removed\n",
			ret);
#endif

}
