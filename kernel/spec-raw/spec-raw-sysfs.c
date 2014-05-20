/*
 * Copyright (C) 2014 CERN (www.cern.ch)
 * Author: Federico Vaga <federico.vaga@cern.ch>
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/fmc.h>

#include "spec-raw.h"

/* Copied from recent kernel 3.11 */
#define __ATTR_RW(_name) __ATTR(_name, (S_IWUSR | S_IRUGO),             \
                          _name##_show, _name##_store)
#define DEVICE_ATTR_RW(_name) \
         struct device_attribute dev_attr_##_name = __ATTR_RW(_name)


/*
 * sr_irq_subscription_show
 * It shows the interrupt subscription list
 */
static ssize_t sr_irq_subscription_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	struct sr_instance *sr = dev_get_drvdata(dev);
	struct ual_irq_status *st;
	int i, count = 0;

	for (i = 0; i < UAL_IRQ_MAX_SUBSCRIPTION; ++i) {
		st = &sr->ual.subscribed_irqs[i];
		if (st->source == 0xBADC0FFE)
			continue;
		count += sprintf(buf + count, "0x%x 0x%x\n",
				 st->source, st->status);
	}

	return count;
}

/*
 * sr_irq_subscription_show
 * It shows the interrupt subscription list
 */
static ssize_t sr_irq_subscription_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct sr_instance *sr = dev_get_drvdata(dev);
	struct ual_irq_status *st;
	uint32_t src, off, msk;
	int i, err;
	char op;

	sscanf(buf, "%c 0x%x 0x%x 0x%x", &op, &src, &off, &msk);

	/* According to the operation add or remove the interrupt */
	switch (op) {
	case '+':
		for (i = 0; i < UAL_IRQ_MAX_SUBSCRIPTION; ++i) {
			st = &sr->ual.subscribed_irqs[i];
			if ((st->source == 0xBADC0FFE) || st->source == src)
				break;
		}

		if (i == UAL_IRQ_MAX_SUBSCRIPTION) {
			dev_err(dev, "subscription list is full\n");
			return -ENOMEM;
		}

		/* add or update a subscription */
		st->source = src;
		st->status = msk;
		st->offset = off;
		/* if it is not the DMA interrupt, then register a new handler */
		if (st->source != sr->irq_dma_base_addr) {
			sr->fmc->irq = st->source;
			err = sr->fmc->op->irq_request(sr->fmc, sr_irq_generic_handler,
						       "spec-raw", 0);
			if (err) {
				sr_irq_status_clean(st);
				dev_err(&sr->fmc->dev,
					"can't request irq %i (error %i)\n",
					sr->fmc->irq, err);
				return err;
			}
		}
		break;
	case '-':
		for (i = 0; i < UAL_IRQ_MAX_SUBSCRIPTION; ++i) {
			st = &sr->ual.subscribed_irqs[i];
			if (st->source == src)
				break;
		}
		if (i == UAL_IRQ_MAX_SUBSCRIPTION) {
		        dev_err(dev, "subscription not found for source 0x%x\n",
				src);
			return -ENODEV;
		}

		/* remove a subscription */
		if (st->source != sr->irq_dma_base_addr) {
			sr->fmc->irq = st->source;
			sr->fmc->op->irq_free(sr->fmc);
		}
		sr_irq_status_clean(st);
		break;
	}
	return count;
}

static DEVICE_ATTR_RW(sr_irq_subscription);


int sr_irq_create_sysfs(struct sr_instance *sr)
{
	return device_create_file(sr->ual.m_irq.this_device,
					  &dev_attr_sr_irq_subscription);
}

void sr_irq_remove_sysfs(struct sr_instance *sr)
{
        device_remove_file(sr->ual.m_irq.this_device,
				   &dev_attr_sr_irq_subscription);
}
