/*
 * Copyright (C) 2014 CERN (www.cern.ch)
 * Author: Federico Vaga <federico.vaga@cern.ch>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 *
 * FIXME most of the DMA code is copied from the fmc-adc-100m14bcha driver
 * (release 2014-05) for a quick and dirty solution while waiting for the
 * better one
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/delay.h>
#include <linux/fmc-sdb.h>
#include <linux/fmc.h>

#include "gncore-dma.h"



/**
 * gncore_calculate_nents
 * It calculates the necessary nents
 */
static int gncore_calculate_nents(void *buf, size_t len)
{
	int bytesleft;
	void *bufp;
	int mapbytes;
	int nents = 0;


	bytesleft = len;
	bufp = buf;
	while (bytesleft) {
		nents++;
		if (bytesleft < (PAGE_SIZE - offset_in_page(bufp)))
			mapbytes = bytesleft;
		else
			mapbytes = PAGE_SIZE - offset_in_page(bufp);
		bufp += mapbytes;
		bytesleft -= mapbytes;
	}

	return nents;
}



/**
 * gncore_setup_dma_scatter
 * It initializes each element of the scatter list
 */
static void gncore_setup_dma_scatter(struct sg_table *sgt, void *buf, size_t len)
{
	struct scatterlist *sg;
	int bytesleft = 0;
	void *bufp = NULL;
	int mapbytes;
	int i, i_blk;

	i_blk = 0;
	bytesleft = len;
	bufp = buf;
	for_each_sg(sgt->sgl, sg, sgt->nents, i) {
		/*
		 * If there are less bytes left than what fits
		 * in the current page (plus page alignment offset)
		 * we just feed in this, else we stuff in as much
		 * as we can.
		 */
		if (bytesleft < (PAGE_SIZE - offset_in_page(bufp)))
			mapbytes = bytesleft;
		else
			mapbytes = PAGE_SIZE - offset_in_page(bufp);
		/* Map the page */
		if (is_vmalloc_addr(bufp))
			sg_set_page(sg, vmalloc_to_page(bufp), mapbytes,
				    offset_in_page(bufp));
		else
			sg_set_buf(sg, bufp, mapbytes);
		/* Configure next values */
		bufp += mapbytes;
		bytesleft -= mapbytes;
		pr_debug("sg item (%p(+0x%lx), len:%d, left:%d)\n",
			 virt_to_page(bufp), offset_in_page(bufp),
			 mapbytes, bytesleft);
	}
}

void gncore_dma_map_page(struct gncore *gncore, int page_idx,
			 struct gncore_dma_item *item,
			   dma_addr_t dma_addr, struct scatterlist *sg,
			   uint32_t dev_mem_off)
{
	/* Prepare DMA item */
	item->start_addr = dev_mem_off;
	item->dma_addr_l = sg_dma_address(sg) & 0xFFFFFFFF;
	item->dma_addr_h = (uint64_t)sg_dma_address(sg) >> 32;
	item->dma_len = sg_dma_len(sg);
	dev_mem_off += item->dma_len;
	if (!sg_is_last(sg)) {/* more transfers */
		/* uint64_t so it works on 32 and 64 bit */
		item->next_addr_l = ((uint64_t)dma_addr) & 0xFFFFFFFF;
		item->next_addr_h = ((uint64_t)dma_addr) >> 32;
		item->attribute = 0x1;	/* more items */
	} else {
		item->attribute = 0x0;	/* last item */
	}
	/* set the DMA direction 0x2 (write), 0x0 (read) */
	item->attribute |=
		(gncore->direction ==  DMA_TO_DEVICE ? (1 << 1) : 0);


	pr_debug("configure DMA item %d "
		 "(addr: 0x%llx len: %d)(dev off: 0x%x)"
		 "(next item: 0x%x)\n",
		 page_idx, (long long)sg_dma_address(sg),
		 sg_dma_len(sg), dev_mem_off, item->next_addr_l);

	/* The first item is written on the device */
	if (page_idx == 0) {
		fmc_writel(gncore->fmc, item->start_addr,
			   gncore->dma_base_addr + GNCORE_DMA_ADDR);
		fmc_writel(gncore->fmc,item->dma_addr_l,
			   gncore->dma_base_addr + GNCORE_DMA_ADDR_L);
		fmc_writel(gncore->fmc, item->dma_addr_h,
			   gncore->dma_base_addr + GNCORE_DMA_ADDR_H);
		fmc_writel(gncore->fmc, item->dma_len,
			   gncore->dma_base_addr + GNCORE_DMA_LEN);
		fmc_writel(gncore->fmc, item->next_addr_l,
			   gncore->dma_base_addr + GNCORE_DMA_NEXT_L);
		fmc_writel(gncore->fmc, item->next_addr_h,
			   gncore->dma_base_addr + GNCORE_DMA_NEXT_H);
		/* Set that there is a next item */
		fmc_writel(gncore->fmc, item->attribute,
			   gncore->dma_base_addr + GNCORE_DMA_BR);
	}
}
EXPORT_SYMBOL(gncore_dma_map_page);



/**
 * gncore_map_dma
 * It maps a given buffer for DMA transfer with a scatterlist. It also maps
 * a smaller DMA memory for the scatterlist
 */
static int gncore_map_dma(struct gncore *gncore)
{
	enum dma_data_direction direction = gncore->direction;
	struct device *dev = gncore->fmc->hwdev;
	struct scatterlist *sg;
	struct gncore_dma_item *items;
	uint32_t dev_mem_off = 0;
	unsigned int i, pages, sglen, size;
	dma_addr_t tmp;
	int err;

	pages = gncore_calculate_nents(gncore->buf, gncore->len);
	if (!pages) {
		dev_info(dev, "No pages to transfer %zu bytes\n",
			gncore->len);
		return -EINVAL;
	}

	dev_dbg(dev, "using %d pages to transfer %zu bytes\n",
		pages, gncore->len);

	/* Create sglists for the transfers */
	err = sg_alloc_table(&gncore->sgt, pages, GFP_ATOMIC);
	if (err) {
		dev_err(dev, "cannot allocate sg table (%i pages)\n", pages);
		return -ENOMEM;
	}

	/* Limited to 32-bit (kernel limit) */
	size = sizeof(struct gncore_dma_item) * gncore->sgt.nents;
	items = kzalloc(size, GFP_KERNEL);
	if (!items) {
		dev_err(dev, "cannot allocate coherent dma memory\n");
		goto out_mem;
	}
	gncore->items = items;
	gncore->dma_list_item = dma_map_single(dev, gncore->items, size,
					       DMA_TO_DEVICE);
	if (!gncore->dma_list_item)
		goto out_free;

	/* Setup the scatter list for the provided block */
	gncore_setup_dma_scatter(&gncore->sgt, gncore->buf, gncore->len);
	/* Map DMA buffers */
	sglen = dma_map_sg(dev, gncore->sgt.sgl, gncore->sgt.nents, direction);
	if (!sglen) {
		dev_err(dev, "cannot map dma memory\n");
		goto out_map;
	}

	/* Configure DMA items */
	dev_mem_off = gncore->offset;
	for_each_sg(gncore->sgt.sgl, sg, gncore->sgt.nents, i) {
		tmp = gncore->dma_list_item;
		tmp += (sizeof(struct gncore_dma_item) * (i + 1));
		gncore_dma_map_page(gncore, i, &items[i], tmp, sg, dev_mem_off);
	}

	return 0;

out_map:
	dma_unmap_single(dev, gncore->dma_list_item, size, DMA_TO_DEVICE);
out_free:
	kfree(gncore->items);
out_mem:
	sg_free_table(&gncore->sgt);

	return -ENOMEM;
}



/**
 * gncore_unmap_dma
 * It unmaps the DMA memory of the buffer and of the scatterlist
 */
static void gncore_unmap_dma(struct gncore *gncore)
{
	struct device *dev = gncore->fmc->hwdev;
	unsigned int size;

	dev_dbg(gncore->fmc->hwdev, "GN-CORE unmap DMA\n");
	size = sizeof(struct gncore_dma_item) * gncore->sgt.nents;
	dma_unmap_single(dev, gncore->dma_list_item, size,
			 DMA_TO_DEVICE);
	dma_unmap_sg(dev, gncore->sgt.sgl, gncore->sgt.nents,
		     gncore->direction);

	kfree(gncore->items);
	gncore->items = NULL;
	gncore->dma_list_item = 0;
	sg_free_table(&gncore->sgt);
}



/**
 * gncore_dma_done
 * It unmaps the DMA memory
 */
static void gncore_dma_over(struct gncore *gncore, unsigned int status)
{
	dev_dbg(&gncore->fmc->dev, "DMA is over\n");

	gncore_unmap_dma(gncore);

	/* Now data is ready */
	spin_lock(&gncore->lock);
	gncore->flags &= ~GNCORE_FLAG_DMA_RUNNING;
	spin_unlock(&gncore->lock);

	/* call user callback to notify that DMA is over */
	if (gncore->dma_over_cb)
		gncore->dma_over_cb(gncore->fmc, status);
}



/**
 * gncore_is_dma_over
 * It returns 1 if the DMA transfer is running
 */
int gncore_dma_is_running(struct gncore *gncore)
{
	int ret;

	spin_lock(&gncore->lock);
	ret = !!(gncore->flags & GNCORE_FLAG_DMA_RUNNING);
	spin_unlock(&gncore->lock);

	return ret;
}
EXPORT_SYMBOL(gncore_dma_is_running);



/**
 * gncore_irq_dma_handler
 * It is not a real IRQ handler but it is a work handler. The reason come from
 * the UAL interface. We want to be transparent as possible and requiring
 * an IRQ handler to the kernel is not a transparent action. Moreover, it does
 * not allow to run DMA even if a driver is loaded.
 *
 * Here, every 5ms we check the DMA's IRQ status register until something
 * happen or someone abort the DMA transfer.
 */
static void gncore_dma_handler(struct work_struct *work)
{
	struct gncore *gncore = container_of(work, struct gncore, dma_work);
	uint32_t status;
	int i;

	/* Wait until DMA is over */
	for (i = 5000; i >= 0; --i) {
		status = fmc_readl(gncore->fmc,
				   gncore->dma_base_addr + GNCORE_DMA_STA);
		if (status != GNCORE_DMA_BUSY)
			break;
		msleep(5);
	}

	/* DMA is over, let's check why  -- I didn't forget 'break' */
	switch (status) {
	case GNCORE_DMA_IDLE:
		/* at this point IDLE state is invalid */
		dev_err(gncore->fmc->hwdev,
			"DMA engine state machine invalid state\n");
	case GNCORE_DMA_BUSY:
		/* Still busy :/ */
		dev_err(gncore->fmc->hwdev,
			"DMA engine is taking too much time, abort it\n");
		/* Abort DMA transfer */
		fmc_writel(gncore->fmc, 0x2,
			   gncore->dma_base_addr + GNCORE_DMA_CTL);
	case GNCORE_DMA_ERROR:
		dev_err(gncore->fmc->hwdev,"DMA engine Error\n");
	default:
		/* Success, Error, Abort  */
		gncore_dma_over(gncore, status);
		break;
	}
}



/**
 * gncore_do_dma
 * It maps the local buffer for DMA according to the last DMA configuration of
 * the current instance. Then, it starts the DMA transfer.
 */
int gncore_dma_run(struct gncore *gncore, void *buf, size_t len,
		    unsigned long offset, enum dma_data_direction direction,
		    void (*dma_over_cb)(void *data, uint32_t status))
{
	int res;

	if (unlikely(!gncore))
		return -ENODEV;
	if (gncore_dma_is_running(gncore))
		return -EBUSY;

	dev_dbg(&gncore->fmc->dev, "Start DMA\n");

	/* Save DMA configuration data */
	gncore->buf = buf;
	gncore->len = len;
	gncore->offset = offset;
	gncore->direction = direction;
	gncore->dma_over_cb = dma_over_cb;

	/* Now the DMA engine is considered 'running' */
	spin_lock(&gncore->lock);
	gncore->flags |= GNCORE_FLAG_DMA_RUNNING;
	spin_unlock(&gncore->lock);

	/* Map DMA */
	res = gncore_map_dma(gncore);
	if (res) {
		spin_lock(&gncore->lock);
		gncore->flags &= ~GNCORE_FLAG_DMA_RUNNING;
		spin_unlock(&gncore->lock);
		return res;
	}

	/* Start DMA transfer */
	fmc_writel(gncore->fmc, 0x1, gncore->dma_base_addr + GNCORE_DMA_CTL);

	/* Schedule work (with high priority ?) */
	schedule_work(&gncore->dma_work);

	return 0;
}
EXPORT_SYMBOL(gncore_dma_run);



/**
 * gncore_dma_init
 * It prepare the gncore structure to perform DMA transfers
 */
struct gncore *gncore_dma_init(struct fmc_device *fmc)
{
	struct gncore *gncore;

	/* create an instance for the gncore */
	gncore = kzalloc(sizeof(struct gncore), GFP_ATOMIC);
	if (!gncore)
		return ERR_PTR(-ENOMEM);
	gncore->fmc = fmc;
	spin_lock_init(&gncore->lock);

	/* Looking for the GENNUM CORE DMA engine */
	gncore->dma_base_addr = fmc_find_sdb_device(gncore->fmc->sdb,
						    0xce42, 0x601, NULL);

	if (gncore->dma_base_addr == -ENODEV) {
		dev_err(fmc->hwdev, "DMA engine is missing\n");
		return ERR_PTR(-ENODEV);
	}

	/* Register DMA handler */
	INIT_WORK(&gncore->dma_work, gncore_dma_handler);
	/* We need special workqueue for performance? */

	return gncore;
}
EXPORT_SYMBOL(gncore_dma_init);



/**
 * gncore_dma_exit
 * It release resources and cleans up memory
 */
void gncore_dma_exit(struct gncore *gncore)
{
	if (!gncore)
		return;

	kfree(gncore);
}
EXPORT_SYMBOL(gncore_dma_exit);
