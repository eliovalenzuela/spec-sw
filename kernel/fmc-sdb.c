/*
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 *
 * This work is part of the White Rabbit project, a research effort led
 * by CERN, the European Institute for Nuclear Research.
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fmc.h>
#include <linux/sdb.h>
#include <linux/err.h>
#include <linux/fmc-sdb.h>
#include <asm/byteorder.h>

static uint32_t __sdb_rd(struct fmc_device *fmc, unsigned long address,
			int convert)
{
	uint32_t res = fmc_readl(fmc, address);
	if (convert)
		return __be32_to_cpu(res);
	return res;
}

static struct sdb_array *__fmc_scan_sdb_tree(struct fmc_device *fmc,
					     unsigned long address, int level)
{
	uint32_t onew;
	int i, j, n, convert = 0;
	struct sdb_array *arr, *sub;

	onew = fmc_readl(fmc, address);
	if (onew == SDB_MAGIC) {
		/* Uh! If we are little-endian, we must convert */
		if (SDB_MAGIC != __be32_to_cpu(SDB_MAGIC))
			convert = 1;
	} else if (onew == __be32_to_cpu(SDB_MAGIC)) {
		/* ok, don't convert */
	} else {
		return ERR_PTR(-ENOENT);
	}
	/* So, the magic was there: get the count from offset 4*/
	onew = __sdb_rd(fmc, address + 4, convert);
	n = __be16_to_cpu(*(uint16_t *)&onew);
	dev_info(fmc->hwdev, "address %lx, %i items (%08x) - c %i\n", address,
		 n, onew, convert);
	arr = kzalloc(sizeof(*arr), GFP_KERNEL);
	if (arr) {
		arr->record = kzalloc(sizeof(arr->record[0]) * n, GFP_KERNEL);
		arr->subtree = kzalloc(sizeof(arr->subtree[0]) * n, GFP_KERNEL);
	}
	if (!arr || !arr->record || !arr->subtree) {
		kfree(arr->record);
		kfree(arr->subtree);
		kfree(arr);
		return ERR_PTR(-ENOMEM);
	}
	arr->len = n;
	arr->level = level;
	arr->fmc = fmc;
	for (i = 0; i < n; i++) {
		union  sdb_record *r;

		for (j = 0; j < sizeof(arr->record[0]); j += 4) {
			*(uint32_t *)((void *)(arr->record + i) + j) =
				__sdb_rd(fmc, address + (i * 64) + j, convert);
		}
		r = &arr->record[i];
		arr->subtree[i] = ERR_PTR(-ENODEV);
		if (r->empty.record_type == sdb_type_bridge) {
			uint64_t subaddr = r->bridge.sdb_child;
			struct sdb_component *c;

			c = &r->bridge.sdb_component;
			subaddr = __be64_to_cpu(subaddr);
			sub = __fmc_scan_sdb_tree(fmc, subaddr, level + 1);
			arr->subtree[i] = sub; /* may be error */
			if (IS_ERR(sub))
				continue;
			sub->parent = arr;
			sub->baseaddr = __be64_to_cpu(c->addr_first);
		}
	}
	return arr;
}

int fmc_scan_sdb_tree(struct fmc_device *fmc, unsigned long address)
{
	struct sdb_array *ret;
	if (fmc->sdb)
		return -EBUSY;
	ret = __fmc_scan_sdb_tree(fmc, address, 0);
	if (IS_ERR(ret))
		return PTR_ERR(ret);
	fmc->sdb = ret;
	return 0;
}
EXPORT_SYMBOL(fmc_scan_sdb_tree);

static void __fmc_sdb_free(struct sdb_array *arr)
{
	int i, n;

	if (!arr) return;
	n = arr->len;
	for (i = 0; i < n; i++) {
		if (IS_ERR(arr->subtree[i]))
			continue;
		__fmc_sdb_free(arr->subtree[i]);
	}
	kfree(arr->record);
	kfree(arr->subtree);
	kfree(arr);
}

int fmc_free_sdb_tree(struct fmc_device *fmc)
{
	__fmc_sdb_free(fmc->sdb);
	fmc->sdb = NULL;
	return 0;
}
EXPORT_SYMBOL(fmc_free_sdb_tree);

static void __fmc_show_sdb_tree(struct sdb_array *arr)
{
	int i, j, n = arr->len, level = arr->level;
	struct sdb_array *ap;

	for (i = 0; i < n; i++) {
		unsigned long base;
		union  sdb_record *r;
		struct sdb_product *p;
		struct sdb_component *c;
		for (j = 0; j < level; j++)
			printk("   ");
		r = &arr->record[i];
		c = &r->dev.sdb_component;
		p = &c->product;
		base = 0;
		for (ap = arr; ap; ap = ap->parent)
			base += ap->baseaddr;

		switch(r->empty.record_type) {
		case sdb_type_interconnect:
			printk("%08llx:%08x %.19s\n",
			       __be64_to_cpu(p->vendor_id),
			       __be32_to_cpu(p->device_id),
			       p->name);
			break;
		case sdb_type_device:
			printk("%08llx:%08x %.19s (%08llx-%08llx)\n",
			       __be64_to_cpu(p->vendor_id),
			       __be32_to_cpu(p->device_id),
			       p->name,
			       __be64_to_cpu(c->addr_first) + base,
			       __be64_to_cpu(c->addr_last) + base);
			break;
		case sdb_type_bridge:
			printk("%08llx:%08x %.19s (bridge: %08llx)\n",
			       __be64_to_cpu(p->vendor_id),
			       __be32_to_cpu(p->device_id),
			       p->name,
			       __be64_to_cpu(c->addr_first) + base);
			if (IS_ERR(arr->subtree[i])) {
				printk("(bridge error %li)\n",
				       PTR_ERR(arr->subtree[i]));
				break;
			}
			__fmc_show_sdb_tree(arr->subtree[i]);
			break;
		case sdb_type_integration:
			printk("integration\n");
			break;
		case sdb_type_empty:
			printk("empty\n");
			break;
		default:
			printk("UNKNOWN TYPE 0x%02x\n", r->empty.record_type);
			break;
		}
	}
}

void fmc_show_sdb_tree(struct fmc_device *fmc)
{
	if (!fmc->sdb)
		return;
	__fmc_show_sdb_tree(fmc->sdb);
}
EXPORT_SYMBOL(fmc_show_sdb_tree);

signed long fmc_find_sdb_device(struct sdb_array *tree,
				uint64_t vid, uint32_t did)
{
	signed long res = -ENODEV;
	union  sdb_record *r;
	struct sdb_product *p;
	struct sdb_component *c;
	int i, n = tree->len;

	/* FIXME: what if the first interconnect is not at zero? */
	for (i = 0; i < n; i++) {
		r = &tree->record[i];
		c = &r->dev.sdb_component;
		p = &c->product;

		if (!IS_ERR(tree->subtree[i]))
			res = fmc_find_sdb_device(tree->subtree[i], vid, did);
		if (res > 0)
			return res + tree->baseaddr;
		if (r->empty.record_type != sdb_type_device)
			continue;
		if (__be64_to_cpu(p->vendor_id) != vid)
			continue;
		if (__be32_to_cpu(p->device_id) != did)
			continue;
		return __be64_to_cpu(c->addr_first);
	}
	return res;
}
EXPORT_SYMBOL(fmc_find_sdb_device);
