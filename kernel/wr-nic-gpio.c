/*
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 *
 * This work is part of the White Rabbit project, a research effort led
 * by CERN, the European Institute for Nuclear Research.
 */
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/fmc.h>
#include "spec-nic.h"

static inline struct fmc_device *gc_to_fmc(struct gpio_chip *gc)
{
	return container_of(gc_to_dev(gc), struct fmc_device, dev);
}

static int wrn_gpio_input(struct gpio_chip *chip, unsigned offset)
{
	//struct fmc_device *fmc = gc_to_fmc(chip);
	//struct wrn_drvdata *dd = fmc_get_drvdata(fmc);

	//fmc_writel(fmc, ...); /*  FIXME  */
	return -EAGAIN;
}

static int wrn_gpio_output(struct gpio_chip *chip, unsigned offset, int value)
{
	return -EAGAIN;
}

int wrn_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	return -EAGAIN;
}

void wrn_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	return;
}

static const char *wrn_gpio_names[] = {
	"dire", "fare", "baciare", "lettera", "testamento"
};

static struct gpio_chip wrn_gpio_template = {
	.label = "wr-nic",
	.owner = THIS_MODULE,
	/* FIXME: request, free, for multi-function operation */
	.direction_input = wrn_gpio_input,
	.direction_output = wrn_gpio_output,
	.get = wrn_gpio_get,
	.set = wrn_gpio_set,
	.base = -1, /* request dynamic */
	.ngpio = 5,
	.names = wrn_gpio_names,
};

int wrn_gpio_init(struct fmc_device *fmc)
{
	struct wrn_drvdata *dd = fmc_get_drvdata(fmc);
	struct gpio_chip *gc;
	int ret;

	gc = devm_kzalloc(&fmc->dev, sizeof(*gc), GFP_KERNEL);
	if (!gc)
		return -ENOMEM;
	*gc = wrn_gpio_template;
	gc_assign_dev(gc, &fmc->dev);

	ret = gpiochip_add(gc);
	if (ret < 0)
		goto out_free;
	dd->gc = gc;

	/* FIXME: program the DAC for each port (sysfs attributes?) */
	return 0;

out_free:
	kfree(gc);
	return ret;
}

void wrn_gpio_exit(struct fmc_device *fmc)
{
	struct wrn_drvdata *dd = fmc_get_drvdata(fmc);

	gpiochip_remove_compat(dd->gc);
}
