/*
 * Copyright (C) 2014 CERN (www.cern.ch)
 * Author: Federico Vaga <federico.vaga@cern.ch>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 */

#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/fmc.h>
#include <linux/workqueue.h>

#define GNCORE_DDR_SIZE 0x10000000 /* 256M */


#define GNCORE_IRQ_DMA_DISABLE_MASK	0x00
#define GNCORE_IRQ_DMA_ENABLE_MASK	0x04
#define GNCORE_IRQ_DMA_MASK_STATUS	0x08
#define GNCORE_IRQ_DMA_SRC		0x0C
#define GNCORE_IRQ_DMA_MASK		0x3

#define GNCORE_IRQ_VIC_CTRL		0x00
#define GNCORE_IRQ_VIC_ENABLE		0x08
#define GNCORE_IRQ_VIC_DISABLE		0x0C
#define GNCORE_IRQ_VIC_STATUS		0x10

/* FIXME enable interrupt source 0 [DMA] (check with Tom Levens is it
 * does not work)
 */
#define GNCORE_IRQ_VIC_MASK		0x1


#define GNCORE_IRQ_DMA_DONE		0x1
#define GNCORE_IRQ_DMA_ERR		0x2

#define GNCORE_DMA_CTL		0x00
#define GNCORE_DMA_CTL_SWP	0xC
#define GNCORE_DMA_CTL_ABORT	0x2
#define GNCORE_DMA_CTL_START	0x1
#define GNCORE_DMA_STA		0x04
#define GNCORE_DMA_ADDR		0x08
#define GNCORE_DMA_ADDR_L	0x0C
#define GNCORE_DMA_ADDR_H	0x10
#define GNCORE_DMA_LEN		0x14
#define GNCORE_DMA_NEXT_L	0x18
#define GNCORE_DMA_NEXT_H	0x1C
#define GNCORE_DMA_BR		0x20
#define GNCORE_DMA_BR_DIR	0x2
#define GNCORE_DMA_BR_LAST	0x1


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
struct gncore_dma_item {
	uint32_t start_addr;	/* 0x00 */
	uint32_t dma_addr_l;	/* 0x04 */
	uint32_t dma_addr_h;	/* 0x08 */
	uint32_t dma_len;	/* 0x0C */
	uint32_t next_addr_l;	/* 0x10 */
	uint32_t next_addr_h;	/* 0x14 */
	uint32_t attribute;	/* 0x18 */
	uint32_t reserved;	/* ouch */
};


struct gncore {
	struct fmc_device *fmc;
	unsigned int dma_base_addr;
	unsigned int irq_dma_base_addr;

	unsigned long flags;

	unsigned long int offset;
	void *buf;
	size_t len;
	enum dma_data_direction direction;
	void (*dma_over_cb)(void *data, uint32_t status);

	struct sg_table sgt;
	dma_addr_t dma_list_item;
	struct gncore_dma_item *items;

	struct spinlock lock;

	uint32_t irq_enable_orig; /* original enable status */
	struct work_struct dma_work;

	void *priv;
};

#define GNCORE_FLAG_DMA_RUNNING (1 << 0)
#define GNCORE_FLAG_DMA_FAIL (1 << 1)

/* Status of the DMA engine */
enum gncore_dma_status {
	GNCORE_DMA_IDLE = 0,
	GNCORE_DMA_DONE,
	GNCORE_DMA_BUSY,
	GNCORE_DMA_ERROR,
	GNCORE_DMA_ABORT,
};

extern struct gncore *gncore_dma_init(struct fmc_device *fmc);
extern void gncore_dma_exit(struct gncore *gncore);
extern int gncore_dma_run(struct gncore *gncore, void *buf, size_t len,
			   unsigned long offset, enum dma_data_direction direction,
			   void (*dma_over_cb)(void *data, uint32_t status));
extern int gncore_dma_is_running(struct gncore *gncore);
extern void gncore_dma_map_page(struct gncore *gncore, int page_idx,
				struct gncore_dma_item *item,
				dma_addr_t dma_addr, struct scatterlist *sg,
				uint32_t dev_mem_off);
