#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/fmc.h>
#include "spec.h"

irqreturn_t t_handler(int irq, void *dev_id)
{
	struct fmc_device *fmc = dev_id;
	struct spec_dev *spec = fmc->carrier_data;
	/* FIXME: don't assume to be under spec */

	dev_info(&spec->pdev->dev, "irq %i\n", irq);
	return IRQ_HANDLED;
}

int t_probe(struct fmc_device *fmc)
{
	int ret;

	ret = request_irq(fmc->irq, t_handler, 0, "fmc-trivial", fmc);
	return ret;
}

int t_remove(struct fmc_device *fmc)
{
	free_irq(fmc->irq, fmc);
	return 0;
}

static struct fmc_driver t_drv = {
	.probe = t_probe,
	.remove = t_remove,
	/* no table, as the current match just matches everything */
};

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
