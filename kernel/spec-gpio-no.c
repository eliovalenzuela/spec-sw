#include <linux/kernel.h>
#include <linux/fmc.h>
#include "spec.h"
/*
 * If the host computer has no gpiolib, this default will apply
 */
int __weak spec_gpio_init(struct fmc_device *fmc)
{
	printk("%s - %s\n", __FILE__,  __func__);
	return 0;
}
void __weak spec_gpio_exit(struct fmc_device *fmc)
{
	printk("%s - %s\n", __FILE__,  __func__);
}


