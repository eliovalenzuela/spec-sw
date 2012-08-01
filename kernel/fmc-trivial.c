/* A trivial fmc driver that can load a gateware file and reports interrupts */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/fmc.h>
#include "spec.h"

static char *t_filename;
module_param_named(file, t_filename, charp, 0444);

irqreturn_t t_handler(int irq, void *dev_id)
{
	struct fmc_device *fmc = dev_id;

	fmc->op->irq_ack(fmc);
	printk("%s: irq %i\n", __func__, irq);
	return IRQ_HANDLED;
}

int t_probe(struct fmc_device *fmc)
{
	int ret;

	ret = fmc->op->irq_request(fmc, t_handler, "fmc-trivial", 0);
	if (ret < 0)
		return ret;

	/* Reprogram, but only if specified in the arguments */
	ret = fmc->op->reprogram(fmc, "");
	if (ret < 0)
		fmc->op->irq_free(fmc);
	return ret;
}

int t_remove(struct fmc_device *fmc)
{
	fmc->op->irq_free(fmc);
	return 0;
}

static struct fmc_driver t_drv = {
	.driver.name = KBUILD_MODNAME,
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

MODULE_LICENSE("GPL and additional rights"); /* public domain */
