/*
* Copyright (C) 2013 CERN (www.cern.ch)
* Author: Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
*
* Released according to the GNU GPL, version 2 or any later version
*
* Driver for SPEC (Simple PCI Express FMC carrier) board.
* VIC (Vectored Interrupt Controller) support code.
*/

#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/fmc.h>
#include <linux/fmc-sdb.h>

#include "spec.h"

#include "hw/vic_regs.h"

#define VIC_MAX_VECTORS 32

#define VIC_SDB_VENDOR 0xce42
#define VIC_SDB_DEVICE 0x0013

/* A Vectored Interrupt Controller object */
struct vic_irq_controller {
	/* It protects the handlers' vector */
	spinlock_t vec_lock;
	/* already-initialized flag */
	int initialized;
	/* Base address (FPGA-relative) */
	uint32_t base;
	/* Mapped base address of the VIC */
	void *kernel_va;

	/* Vector table */
	struct vector {
		/* Saved ID of the vector (for autodetection purposes) */
		int saved_id;
		/* Pointer to the assigned handler */
		irq_handler_t handler;
		/* FMC device that owns the interrupt */
		struct fmc_device *requestor;
	} vectors[VIC_MAX_VECTORS];
};

static inline void vic_writel(struct vic_irq_controller *vic, uint32_t value,
			      uint32_t offset)
{
	writel(value, vic->kernel_va + offset);
}

static inline uint32_t vic_readl(struct vic_irq_controller *vic,
				 uint32_t offset)
{
	return readl(vic->kernel_va + offset);
}

static int spec_vic_init(struct spec_dev *spec, struct fmc_device *fmc)
{
	int i;
	signed long vic_base;
	struct vic_irq_controller *vic;

	/*
	 * Try to look up the VIC in the SDB tree - note that IRQs
	 * shall be requested after the FMC driver has scanned the SDB tree
	 */
	vic_base =
	    fmc_find_sdb_device(fmc->sdb, VIC_SDB_VENDOR, VIC_SDB_DEVICE, NULL);

	if (vic_base < 0) {
		dev_err(&spec->pdev->dev,
			"VIC controller not found, but a VIC interrupt requested. Wrong gateware?\n");
		return -ENODEV;
	}

	dev_info(&spec->pdev->dev, "Found VIC @ 0x%lx\n", vic_base);

	vic = kzalloc(sizeof(struct vic_irq_controller), GFP_KERNEL);
	if (!vic)
		return -ENOMEM;

	spin_lock_init(&vic->vec_lock);
	vic->kernel_va = spec->remap[0] + vic_base;
	vic->base = (uint32_t) vic_base;

	/* disable all IRQs, copy the vector table with pre-defined IRQ ids */
	vic_writel(vic, 0xffffffff, VIC_REG_IDR);
	for (i = 0; i < VIC_MAX_VECTORS; i++)
		vic->vectors[i].saved_id =
			vic_readl(vic, VIC_IVT_RAM_BASE + 4 * i);

	/* config the VIC output: active high, edge, width = 256 tick (4 us) */
	vic_writel(vic,
		   VIC_CTL_ENABLE | VIC_CTL_POL | VIC_CTL_EMU_EDGE |
		   VIC_CTL_EMU_LEN_W(250), VIC_REG_CTL);

	vic->initialized = 1;
	spec->vic = vic;

	return 0;
}

static void spec_vic_exit(struct vic_irq_controller *vic)
{
	if (!vic)
		return;

	/* Disable all irq lines and the VIC in general */
	vic_writel(vic, 0xffffffff, VIC_REG_IDR);
	vic_writel(vic, 0, VIC_REG_CTL);
	kfree(vic);
}

irqreturn_t spec_vic_irq_dispatch(struct spec_dev *spec)
{
	struct vic_irq_controller *vic = spec->vic;
	int index, rv;
	struct vector *vec;

	/*
	 * Our parent IRQ handler: read the index value
	 * from the Vector Address Register, and find matching handler
	 */
	index = vic_readl(vic, VIC_REG_VAR) & 0xff;

	if (index >= VIC_MAX_VECTORS)
		goto fail;

	vec = &vic->vectors[index];
	if (!vec->handler)
		goto fail;

	rv = vec->handler(vec->saved_id, vec->requestor);

	vic_writel(vic, 0, VIC_REG_EOIR);	/* ack the irq */
	return rv;

fail:
	return 0;
}

int spec_vic_irq_request(struct spec_dev *spec, struct fmc_device *fmc,
			 unsigned long id, irq_handler_t handler)
{
	struct vic_irq_controller *vic;
	int rv = 0, i;

	/* First interrupt to be requested? Look up and init the VIC */
	if (!spec->vic) {
		rv = spec_vic_init(spec, fmc);
		if (rv)
			return rv;
	}

	vic = spec->vic;

	for (i = 0; i < VIC_MAX_VECTORS; i++) {
		/* find vector in stored table, assign handle, enable */
		if (vic->vectors[i].saved_id == id) {
			spin_lock(&spec->vic->vec_lock);

			vic_writel(vic, i, VIC_IVT_RAM_BASE + 4 * i);
			vic->vectors[i].requestor = fmc;
			vic->vectors[i].handler = handler;
			vic_writel(vic, (1 << i), VIC_REG_IER);

			spin_unlock(&spec->vic->vec_lock);
			return 0;
		}
	}

	return -EINVAL;
}


/*
 * vic_handler_count
 * It counts how many handlers are registered within the VIC controller
 */
static inline int vic_handler_count(struct vic_irq_controller *vic)
{
	int i, count;

	for (i = 0, count = 0; i < VIC_MAX_VECTORS; ++i)
		if (vic->vectors[i].handler)
			count++;

	return count;
}


void spec_vic_irq_free(struct spec_dev *spec, unsigned long id)
{
	int i;

	for (i = 0; i < VIC_MAX_VECTORS; i++) {
		uint32_t vec = spec->vic->vectors[i].saved_id;
		if (vec == id) {
			spin_lock(&spec->vic->vec_lock);

			vic_writel(spec->vic, 1 << i, VIC_REG_IDR);
			vic_writel(spec->vic, vec, VIC_IVT_RAM_BASE + 4 * i);
			spec->vic->vectors[i].handler = NULL;

			spin_unlock(&spec->vic->vec_lock);
		}
	}

	/* Clean up the VIC if there are no more handlers */
	if (!vic_handler_count(spec->vic)) {
		spec_vic_exit(spec->vic);
		spec->vic = NULL;
	}
}

void spec_vic_irq_ack(struct spec_dev *spec, unsigned long id)
{
	/* fixme: do we need anything special here? */
}
