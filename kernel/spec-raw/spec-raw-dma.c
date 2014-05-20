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
#include <linux/fmc.h>

#include "spec-raw.h"
#include "spec-raw-user.h"

/*
 * sr_calculate_nents
 * It calculates the necessary nents
 */
static int sr_calculate_nents(void *buf, size_t len)
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


/*
 * sr_setup_dma_scatter
 * It initializes each element of the scatter list
 */
static void sr_setup_dma_scatter(struct sg_table *sgt, void *buf, size_t len)
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


/*
 * sr_map_dma
 * It maps a given buffer for DMA transfer with a scatterlist. It also maps
 * a smaller DMA memory for the scatterlist
 */
static int sr_map_dma(struct sr_instance *sr, void *buf, size_t len,
			size_t mem_off)
{
	enum dma_data_direction direction;
	struct device *dev = sr->fmc->hwdev;
	struct scatterlist *sg;
	struct sr_dma_item *items;
	uint32_t dev_mem_off = 0;
	unsigned int i, pages, sglen, size;
	dma_addr_t tmp;
	int err;

	pages = sr_calculate_nents(buf, len);
	if (!pages) {
		dev_info(dev, "No pages to transfer %zu bytes\n",
			len);
		return -EINVAL;
	}
	dev_dbg(dev, "using %d pages to transfer %zu bytes\n", pages, len);

	/* Create sglists for the transfers */
	err = sg_alloc_table(&sr->sgt, pages, GFP_ATOMIC);
	if (err) {
		dev_err(dev, "cannot allocate sg table (%i pages)\n", pages);
		return -ENOMEM;
	}

	/* Limited to 32-bit (kernel limit) */
	size = sizeof(struct sr_dma_item) * sr->sgt.nents;
	items = kzalloc(size, GFP_KERNEL);
	if (!items) {
		dev_err(dev, "cannot allocate coherent dma memory\n");
		goto out_mem;
	}
	sr->items = items;
	sr->dma_list_item = dma_map_single(dev, items, size, DMA_TO_DEVICE);
	if (!sr->dma_list_item)
		goto out_free;

	/* Setup the scatter list for the provided block */
	sr_setup_dma_scatter(&sr->sgt, buf, len);
	/* Map DMA buffers */
	direction = sr->dma.flags & SR_IOCTL_DMA_FLAG_WRITE ? DMA_TO_DEVICE :
							      DMA_FROM_DEVICE;
	sglen = dma_map_sg(dev, sr->sgt.sgl, sr->sgt.nents, direction);
	if (!sglen) {
		dev_err(dev, "cannot map dma memory\n");
		goto out_map;
	}


	/* Configure DMA items */
	dev_mem_off = mem_off;
	for_each_sg(sr->sgt.sgl, sg, sr->sgt.nents, i) {
		/* Prepare DMA item */
		items[i].start_addr = dev_mem_off;
		items[i].dma_addr_l = sg_dma_address(sg) & 0xFFFFFFFF;
		items[i].dma_addr_h = (uint64_t)sg_dma_address(sg) >> 32;
		items[i].dma_len = sg_dma_len(sg);
		dev_mem_off += items[i].dma_len;
		if (!sg_is_last(sg)) {/* more transfers */
			/* uint64_t so it works on 32 and 64 bit */
			tmp = sr->dma_list_item;
			tmp += (sizeof(struct sr_dma_item) * (i + 1));
			items[i].next_addr_l = ((uint64_t)tmp) & 0xFFFFFFFF;
			items[i].next_addr_h = ((uint64_t)tmp) >> 32;
			items[i].attribute = 0x1;	/* more items */
		} else {
			items[i].attribute = 0x0;	/* last item */
		}
		/* set the DMA direction 0x2 (write), 0x0 (read) */
		items[i].attribute |=
		       (sr->dma.flags & SR_IOCTL_DMA_FLAG_WRITE ? (1 << 1) : 0);


		pr_debug("configure DMA item %d "
			"(addr: 0x%llx len: %d)(dev off: 0x%x)"
			"(next item: 0x%x)\n",
			i, (long long)sg_dma_address(sg),
			sg_dma_len(sg), dev_mem_off, items[i].next_addr_l);

		/* The first item is written on the device */
		if (i == 0) {
			fmc_writel(sr->fmc, items[i].start_addr,
				   sr->dma_base_addr + SR_DMA_ADDR);
			fmc_writel(sr->fmc,items[i].dma_addr_l,
				   sr->dma_base_addr + SR_DMA_ADDR_L);
			fmc_writel(sr->fmc, items[i].dma_addr_h,
				   sr->dma_base_addr + SR_DMA_ADDR_H);
			fmc_writel(sr->fmc, items[i].dma_len,
				   sr->dma_base_addr + SR_DMA_LEN);
			fmc_writel(sr->fmc, items[i].next_addr_l,
				   sr->dma_base_addr + SR_DMA_NEXT_L);
			fmc_writel(sr->fmc, items[i].next_addr_h,
				   sr->dma_base_addr + SR_DMA_NEXT_H);
			/* Set that there is a next item */
			fmc_writel(sr->fmc, items[i].attribute,
				   sr->dma_base_addr + SR_DMA_BR);
		}
	}

	return 0;

out_map:
	dma_unmap_single(dev, sr->dma_list_item, size, DMA_TO_DEVICE);
out_free:
	kfree(sr->items);
out_mem:
	sg_free_table(&sr->sgt);

	return -ENOMEM;
}


/*
 * sr_unmap_dma
 * It unmaps the DMA memory of the buffer and of the scatterlist
 */
static void sr_unmap_dma(struct sr_instance *sr)
{
	struct device *dev = &sr->fmc->dev;
	enum dma_data_direction direction;
	unsigned int size;

	dev_dbg(dev, "unmap DMA\n");
	size = sizeof(struct sr_dma_item) * sr->sgt.nents;
	dma_unmap_single(dev, sr->dma_list_item, size, DMA_TO_DEVICE);
	direction = sr->dma.flags & SR_IOCTL_DMA_FLAG_WRITE ? DMA_TO_DEVICE :
							      DMA_FROM_DEVICE;
	dma_unmap_sg(dev, sr->sgt.sgl, sr->sgt.nents, direction);

	kfree(sr->items);
	sr->items = NULL;
	sr->dma_list_item = 0;
	sg_free_table(&sr->sgt);
}


/*
 * sr_dma_start
 * It maps the local buffer for DMA according to the last DMA configuration of
 * the current instance. Then, it starts the DMA transfer.
 */
int sr_dma_start(struct sr_instance *sr)
{
	int res;

	dev_dbg(&sr->fmc->dev, "Start DMA\n");
	res = sr_map_dma(sr, sr->ual.dma_buf, sr->dma.length, sr->dma.dev_mem_off);
	if (res)
		return res;

	/* flag that DMA is running */
	spin_lock(&sr->lock);
	sr->flags &= ~SR_FLAG_DATA_RDY;
	spin_unlock(&sr->lock);

	/* Start DMA transfer */
	fmc_writel(sr->fmc, 0x1, sr->dma_base_addr + SR_DMA_CTL);
	return 0;
}


/*
 * sr_dma_done
 * It unmaps the DMA memory
 */
void sr_dma_done(struct sr_instance *sr)
{
	dev_dbg(&sr->fmc->dev, "DMA is over\n");
	sr_unmap_dma(sr);

	/* Now data is ready */
	spin_lock(&sr->lock);
	sr->flags |= SR_FLAG_DATA_RDY;
	spin_unlock(&sr->lock);
}


/*
 * sr_is_dma_over
 * It returns 1 if the DMA transfer is over and the user can safely read the
 * buffer, 0 otherwise
 */
int sr_is_dma_over(struct sr_instance *sr)
{
	int ret;

	spin_lock(&sr->lock);
	ret = !!(sr->flags & SR_FLAG_DATA_RDY);
	spin_unlock(&sr->lock);

	return ret;
}
