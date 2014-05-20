/*
 * Copyright (C) 2014 CERN (www.cern.ch)
 * Author: Federico Vaga <federico.vaga@cern.ch>
 */

#ifndef SPEC_RAW_H_
#define SPEC_RAW_H_

#include <linux/miscdevice.h>
#include <linux/scatterlist.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/mutex.h>

#include "spec-raw-user.h"
#include "spec-raw-ual.h"

#define SR_DDR_SIZE 0x10000000 /* 256M */


#define SR_IRQ_DMA_DISABLE_MASK	0x00
#define SR_IRQ_DMA_ENABLE_MASK	0x04
#define SR_IRQ_DMA_MASK_STATUS	0x08
#define SR_IRQ_DMA_SRC		0x0C
#define SR_IRQ_DMA_MASK		0x3

#define SR_IRQ_VIC_CTRL		0x00
#define SR_IRQ_VIC_ENABLE	0x08
#define SR_IRQ_VIC_DISABLE	0x0C
#define SR_IRQ_VIC_STATUS	0x10

/* FIXME enable interrupt source 0 [DMA] (check with Tom Levens is it
 * does not work)
 */
#define SR_IRQ_VIC_MASK		0x1


#define SR_IRQ_DMA_DONE		0x1
#define SR_IRQ_DMA_ERR		0x2

#define SR_DMA_CTL		0x00
#define SR_DMA_CTL_SWP		0xC
#define SR_DMA_CTL_ABORT	0x2
#define SR_DMA_CTL_START	0x1
#define SR_DMA_STA		0x04
#define SR_DMA_ADDR		0x08
#define SR_DMA_ADDR_L		0x0C
#define SR_DMA_ADDR_H		0x10
#define SR_DMA_LEN		0x14
#define SR_DMA_NEXT_L		0x18
#define SR_DMA_NEXT_H		0x1C
#define SR_DMA_BR		0x20
#define SR_DMA_BR_DIR		0x2
#define SR_DMA_BR_LAST		0x1

/*
 * fa_dma_item: The information about a DMA transfer
 * @start_addr: pointer where start to retrieve data from device memory
 * @dma_addr_l: low 32bit of the dma address on host memory
 * @dma_addr_h: high 32bit of the dma address on host memory
 * @dma_len: number of bytes to transfer from device to host
 * @next_addr_l: low 32bit of the address of the next memory area to use
 * @next_addr_h: high 32bit of the address of the next memory area to use
 * @attribute: dma information about data transferm. At the moment it is used
 *             only to provide the "last item" bit, direction is fixed to
 *             device->host
 */
struct sr_dma_item {
	uint32_t start_addr;	/* 0x00 */
	uint32_t dma_addr_l;	/* 0x04 */
	uint32_t dma_addr_h;	/* 0x08 */
	uint32_t dma_len;	/* 0x0C */
	uint32_t next_addr_l;	/* 0x10 */
	uint32_t next_addr_h;	/* 0x14 */
	uint32_t attribute;	/* 0x18 */
	uint32_t reserved;	/* ouch */
};

#define SR_FLAG_DATA_RDY (1 << 0)

struct sr_instance {
	struct ual_device ual;

	struct list_head list;
	struct fmc_device *fmc;
	wait_queue_head_t q_dma;
	struct spinlock lock;
	unsigned int flags;
	struct mutex mtx;

	/* Debug FS */
	struct dentry *dbg_dir;
	struct dentry *dma_loopback;
	struct dentry *dma_write_seq;
	struct dentry *dma_write_zero;

	/* DMA */
	unsigned int dma_base_addr;
	struct sr_dma_request dma;
	struct sg_table sgt;
	dma_addr_t dma_list_item;
	struct sr_dma_item *items;

	/* IRQ */
	unsigned int vic_base_addr;
	unsigned int irq_dma_base_addr;
	unsigned int irq_base_addr;
};

extern const struct file_operations sr_reg_fops;
extern const struct file_operations sr_dma_fops;
extern const struct file_operations sr_irq_fops;
extern const struct file_operations sr_dbgfs_dma_loop_op;
extern const struct file_operations sr_dbgfs_dma_write_seq;
extern const struct file_operations sr_dbgfs_dma_write_zero;
extern struct list_head sr_devices;

extern int sr_dma_start(struct sr_instance *sr);
extern void sr_dma_done(struct sr_instance *sr);
extern int sr_is_dma_over(struct sr_instance *sr);
extern int sr_request_irqs(struct sr_instance *sr);
extern void sr_free_irqs(struct sr_instance *sr);

extern int sr_irq_create_sysfs(struct sr_instance *sr);
extern void sr_irq_remove_sysfs(struct sr_instance *sr);

extern irqreturn_t sr_irq_generic_handler(int irq_core_base, void *arg);



static inline int sr_irq_find_subscription(struct sr_instance *sr, uint32_t source)
{
	int i;

	/* Check if the user request notification for this source */
	for (i = 0; i < UAL_IRQ_MAX_SUBSCRIPTION; ++i)
		if (sr->ual.subscribed_irqs[i].source == source )
			break;

	if (i == UAL_IRQ_MAX_SUBSCRIPTION) /* no subscription found */
		return -1;

	return i;
}


#endif /* SPEC_RAW_H_ */
