/*
 * Copyright (C) 2014 CERN (www.cern.ch)
 * Author: Federico Vaga <federico.vaga@cern.ch>
 */
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/fmc.h>

#include "spec-raw.h"


/* Unfortunately, on the spec this is GPIO9, i.e. IRQ(1) */
static struct fmc_gpio fa_gpio_on[] = {
	{
	 .gpio = FMC_GPIO_IRQ(0),
	 .mode = GPIOF_DIR_IN,
	 .irqmode = IRQF_TRIGGER_RISING,
	 }
};

static struct fmc_gpio fa_gpio_off[] = {
	{
	 .gpio = FMC_GPIO_IRQ(0),
	 .mode = GPIOF_DIR_IN,
	 .irqmode = 0,
	 }
};

/*
 * sr_irq_history_add
 */
static inline void sr_irq_history_add(struct sr_instance *sr, struct ual_irq_status *st,
				      uint32_t status)
{
	/* Check if the user request notification for a particular interrupt */
	if (!(st->status & status))
		return;

	sr->ual.last_irqs[sr->ual.w_idx_irq].source = st->source;
	sr->ual.last_irqs[sr->ual.w_idx_irq].status = status;
	sr->ual.last_irqs[sr->ual.w_idx_irq].offset = st->offset;
	sr->ual.w_idx_irq++;
	pr_info("%s %d %d\n", __func__, __LINE__, sr->ual.w_idx_irq);
	if (sr->ual.w_idx_irq == sr->ual.r_idx_irq)
		sr->ual.r_idx_irq++;

	if (unlikely(sr->ual.w_idx_irq >= UAL_IRQ_HISTORY))
		sr->ual.w_idx_irq = 0;
	if (unlikely(sr->ual.r_idx_irq >= UAL_IRQ_HISTORY))
		sr->ual.r_idx_irq = 0;
}

irqreturn_t sr_irq_generic_handler(int irq_core_base, void *arg)
{
	struct fmc_device *fmc = arg;
	struct sr_instance *sr = fmc_get_drvdata(fmc);
	struct ual_irq_status *st;
	uint32_t irq_status;
	int ret;

	ret = sr_irq_find_subscription(sr, irq_core_base);
	if (ret < 0)
		goto out;
	st = &sr->ual.subscribed_irqs[ret];
	irq_status = fmc_readl(fmc, st->source + st->offset);
	/* Clear current interrupts status */
	fmc_writel(fmc, irq_status, st->source + st->offset);
	dev_dbg(&sr->fmc->dev, "Handle interrupts 0x%x 0x%x\n",
		st->source + st->offset, irq_status);

	if (irq_status & st->status) {
		sr_irq_history_add(sr, st, irq_status);
		wake_up_interruptible(&sr->ual.q_irq);
	}

out:
	fmc->op->irq_ack(fmc);

	return IRQ_HANDLED;
}

/*
 * sr_irq_dma_handler
 * It handles the interrupt coming from the DMA engine:
 * - DMA done
 * - DMA error
 */
static irqreturn_t sr_irq_dma_handler(int irq_core_base, void *arg)
{
	struct fmc_device *fmc = arg;
	struct sr_instance *sr = fmc_get_drvdata(fmc);
	uint32_t irq_status;
	int ret, err;

	irq_status = fmc_readl(fmc, sr->irq_dma_base_addr + SR_IRQ_DMA_SRC);
	/* Clear current interrupts status */
	fmc_writel(fmc, irq_status, sr->irq_dma_base_addr + SR_IRQ_DMA_SRC);
	dev_dbg(&sr->fmc->dev, "Handle DMA interrupts 0x%x\n", irq_status);

	if (irq_status & SR_IRQ_DMA_MASK) {
		ret = sr_irq_find_subscription(sr, irq_core_base);
		if (ret >= 0) {
			sr_irq_history_add(sr, &sr->ual.subscribed_irqs[ret], irq_status);
			wake_up_interruptible(&sr->ual.q_irq);
		}
		sr_dma_done(sr);

		/* Wake up all listener */
		wake_up_interruptible(&sr->q_dma);


		if (unlikely(irq_status  & SR_IRQ_DMA_ERR)) {
			err = fmc_readl(sr->fmc, sr->dma_base_addr + SR_DMA_STA);
			if (err)
				dev_err(&sr->fmc->dev,
					"DMA error (status 0x%x)\n", err);
		}
	} else {
		dev_info(&sr->fmc->dev, "Unknown interrupt 0x%x\n", irq_status);
	}

	fmc->op->irq_ack(fmc);

	return IRQ_HANDLED;
}


/*
 * sr_request_irqs
 * If the VIC component is there, then it enables all the necessary interrupt
 * sources and it registers an interrupt handler for each of them:
 * - DMA interrupt controller
 */
int sr_request_irqs(struct sr_instance *sr)
{
	int err = 0;

	/*
	 * If the VIC is not there, we have no interrupt to handle
	 * [some old firmware will not work with this module]
	 */
	if (!sr->vic_base_addr)
		return 0;

	/* Configure the VIC control*/
	/* FIXME check if we have to set also the edge length */
	fmc_writel(sr->fmc, 0x3, sr->vic_base_addr + SR_IRQ_VIC_CTRL);
	/* Enable all interrupts on VIC */
	fmc_writel(sr->fmc, SR_IRQ_VIC_MASK,
		   sr->vic_base_addr + SR_IRQ_VIC_ENABLE);
	fmc_writel(sr->fmc, ~SR_IRQ_VIC_MASK & SR_IRQ_VIC_DISABLE,
		   sr->vic_base_addr + SR_IRQ_VIC_ENABLE);


	/* Request IRQ to VIC for the DMA interrupts */
	if (sr->irq_dma_base_addr) {
		dev_dbg(&sr->fmc->dev, "Request DMA interrupts\n");
		sr->fmc->irq = sr->irq_dma_base_addr;
		err = sr->fmc->op->irq_request(sr->fmc, sr_irq_dma_handler,
					       "spec-raw", 0);
		if (err) {
			dev_err(&sr->fmc->dev,
				"can't request irq %i (error %i)\n",
				sr->fmc->irq, err);
			return err;
		}
		/* Enable interrupts: DMA done and DMA error */
		fmc_writel(sr->fmc, SR_IRQ_DMA_MASK,
			   sr->irq_dma_base_addr + SR_IRQ_DMA_ENABLE_MASK);
		fmc_writel(sr->fmc, ~SR_IRQ_DMA_MASK & SR_IRQ_DMA_MASK ,
			   sr->irq_dma_base_addr + SR_IRQ_DMA_DISABLE_MASK);
	}

	sr->fmc->op->gpio_config(sr->fmc, fa_gpio_on, ARRAY_SIZE(fa_gpio_on));

	return err;
}


/*
 * sr_free_irqs
 * If the VIC component is there, then it frees the IRQ and disable all the
 * interrupt sources
 */
void sr_free_irqs(struct sr_instance *sr)
{
	struct ual_irq_status *st;
	int i;

	if (!sr->vic_base_addr)
		return ;

	dev_dbg(&sr->fmc->dev, "Free all interrupts\n");
	/* Release all subscribed interrupts */
	for (i = 0; i < UAL_IRQ_MAX_SUBSCRIPTION; ++i) {
		st = &sr->ual.subscribed_irqs[i];
		if (st->source == 0xBADC0FFE)
			continue;
		if (st->source != sr->irq_dma_base_addr) {
			sr->fmc->irq = st->source;
			sr->fmc->op->irq_free(sr->fmc);
		}
		sr_irq_status_clean(st);
	}
	/* Release the DMA interrupts */
	sr->fmc->irq = sr->irq_dma_base_addr;
	sr->fmc->op->irq_free(sr->fmc);
	fmc_writel(sr->fmc, 0x0,
		   sr->vic_base_addr + SR_IRQ_VIC_ENABLE);
	fmc_writel(sr->fmc, 0x3,
		   sr->vic_base_addr + SR_IRQ_VIC_DISABLE);

	sr->fmc->op->gpio_config(sr->fmc, fa_gpio_off, ARRAY_SIZE(fa_gpio_off));
}
