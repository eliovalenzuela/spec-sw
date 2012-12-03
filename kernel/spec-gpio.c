
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/fmc.h>
#include "spec.h"

int spec_gpio_init(struct fmc_device *fmc)
{
	pr_debug("%s - %s\n", __FILE__, __func__);
	return 0;
}

void spec_gpio_exit(struct fmc_device *fmc)
{
	pr_debug("%s - %s\n", __FILE__,  __func__);
}
