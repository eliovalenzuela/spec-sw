/* A trivial fmc driver that can load a gateware file and reports interrupts */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/fmc.h>
#include "spec.h"

static struct fmc_driver t_drv; /* initialized later */

irqreturn_t t_handler(int irq, void *dev_id)
{
	struct fmc_device *fmc = dev_id;

	fmc->op->irq_ack(fmc);
	dev_info(fmc->hwdev, "received irq %i\n", irq);
	return IRQ_HANDLED;
}

struct fmc_gpio t_gpio[] = {
	{
		.gpio = FMC_GPIO_IRQ(0),
		.mode = GPIOF_DIR_IN,
		.irqmode = IRQF_TRIGGER_RISING,
	}, {
		.gpio = FMC_GPIO_IRQ(1),
		.mode = GPIOF_DIR_IN,
		.irqmode = IRQF_TRIGGER_RISING,
	}
};

int t_probe(struct fmc_device *fmc)
{
	int ret;
	int index;

	index = fmc->op->validate(fmc, &t_drv);
	if (index < 0)
		return -EINVAL; /* not our device: invalid */

	ret = fmc->op->irq_request(fmc, t_handler, "fmc-trivial", 0);
	if (ret < 0)
		return ret;
	/* ignore error code of call below, we really don't care */
	fmc->op->gpio_config(fmc, t_gpio, ARRAY_SIZE(t_gpio));

	/* Reprogram, if asked to. ESRCH == no filename specified */
	ret = fmc->op->reprogram(fmc, &t_drv,"");
	if (ret == -ESRCH)
		ret = 0;
	if (ret < 0)
		fmc->op->irq_free(fmc);

	/* FIXME: reprogram LM32 too */
	return ret;
}

int t_remove(struct fmc_device *fmc)
{
	fmc->op->irq_free(fmc);
	return 0;
}

static struct fmc_driver t_drv = {
	.version = FMC_VERSION,
	.driver.name = KBUILD_MODNAME,
	.probe = t_probe,
	.remove = t_remove,
	/* no table, as the current match just matches everything */
};

 /* We accept the generic parameters */
FMC_PARAM_BUSID(t_drv);
FMC_PARAM_GATEWARE(t_drv);

static int t_init(void)
{
	int ret;

	ret = fmc_driver_register(&t_drv);
	return ret;
}

static void t_exit(void)
{
	fmc_driver_unregister(&t_drv);
}

module_init(t_init);
module_exit(t_exit);

MODULE_LICENSE("GPL and additional rights"); /* public domain */
