/*
 * Copyright (C) 2015 CERN (www.cern.ch)
 * Author: Federico Vaga <federico.vaga@cern.ch>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 *
 * This work is part of the White Rabbit project, a research effort led
 * by CERN, the European Institute for Nuclear Research.
 */

#include <linux/version.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include "spec.h"

#if LINUX_VERSION_CODE > KERNEL_VERSION(3,9,0)
#include <linux/irqchip/chained_irq.h>
#else

static inline void chained_irq_enter(struct irq_chip *chip,
                                     struct irq_desc *desc)
{
        /* FastEOI controllers require no action on entry. */
        if (chip->irq_eoi)
                return;

        if (chip->irq_mask_ack) {
                chip->irq_mask_ack(&desc->irq_data);
        } else {
                chip->irq_mask(&desc->irq_data);
                if (chip->irq_ack)
                        chip->irq_ack(&desc->irq_data);
        }
}

static inline void chained_irq_exit(struct irq_chip *chip,
                                    struct irq_desc *desc)
{
        if (chip->irq_eoi)
                chip->irq_eoi(&desc->irq_data);
        else
                chip->irq_unmask(&desc->irq_data);
}
#endif


/**
 * the interrupt is over, ack the GN4124 chip
 */
static void gn4124_irq_ack(struct irq_data *d)
{
	struct spec_dev *spec = irq_data_get_irq_chip_data(d);

	/*
	 * Note: we only support gpio interrupts here, i.e. the
	 * 0x814 (INT_STAT) register is expected to only have bit 15 set.
	 * We also accept software-generated irq, but they need no ack.
	 */
	gennum_readl(spec, GNGPIO_INT_STATUS); /* ack the gn4124 */
}

static void gn4124_irq_mask(struct irq_data *d)
{

}

static void gn4124_irq_unmask(struct irq_data *d)
{

}


/**
 * GN4124 IRQ controller descriptor
 */
static struct irq_chip gn4124_ir1_chip = {
	.name = "GN4124",
	.irq_ack = gn4124_irq_ack,
	.irq_mask = gn4124_irq_mask,
	.irq_unmask = gn4124_irq_unmask,
};


/**
 * It maps a local interrupt number (hwirq) to a Linux irq number (virtirq)
 */
static int gn4124_irq_domain_map(struct irq_domain *h,
				 unsigned int virtirq,
				 irq_hw_number_t hwirq)
{
	struct spec_dev *spec = h->host_data;

	irq_set_chip(virtirq, &gn4124_ir1_chip);
	__irq_set_handler(virtirq, handle_edge_irq, 0, NULL);
	irq_set_chip_data(virtirq, spec);
	irq_set_handler_data(virtirq, spec);

	return 0;
}


static struct irq_domain_ops gn4124_irq_domain_ops = {
	.map = gn4124_irq_domain_map,
};


/**
 * Handle cascade IRQ coming from the PCI controller and re-route it properly
 * When the carrier receive an interrupt it will call than this function
 * which then will call the proper handler
 */
static void gn4124_handle_cascade_irq(unsigned int irq, struct irq_desc *desc)
{
	struct spec_dev *spec = irq_get_handler_data(irq);
	struct irq_chip *chip = irq_get_chip(irq);
	unsigned int cascade_irq;

	/* Things to do before on the PCI controller */
	chained_irq_enter(chip, desc);

	/* Things to do at GN4124 level */
	cascade_irq = irq_find_mapping(spec->domain, 0);
	if (unlikely(0)) {
		handle_bad_irq(cascade_irq, desc);
	} else {
		generic_handle_irq(cascade_irq);
	}

	/* Things to do before on the PCI controller */
	chained_irq_exit(chip, desc);
}

/**
 * Configure the GN4124 to be chained to the standard PCI irq controller
 */
int gn4124_irq_domain_create(struct spec_dev *spec)
{
	int ret;

	spec->domain = irq_domain_add_linear(NULL, 1, &gn4124_irq_domain_ops,
					     spec);
	if (!spec->domain)
		return -ENOMEM;

	/* We have only one interrupt line for the time being */
	ret = irq_create_mapping(spec->domain, 0);
	if (!ret) {
		irq_domain_remove(spec->domain);
		return -EINVAL;
	}

	if (irq_set_handler_data(spec->pdev->irq, spec) != 0)
		BUG();
	irq_set_chained_handler(spec->pdev->irq, gn4124_handle_cascade_irq);

	spec->fmc->irq = irq_find_mapping(spec->domain, 0);

	return 0;
}

void gn4124_irq_domain_destroy(struct spec_dev *spec)
{
	irq_domain_remove(spec->domain);
	/* FIX parent IRQ controller */
}
