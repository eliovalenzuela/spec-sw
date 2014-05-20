/*
 * Copyright (C) 2014 CERN (www.cern.ch)
 * Author: Federico Vaga <federico.vaga@cern.ch>
 * Author: Alessandro Rubini <rubini@gnudd.com>
 */
#include <linux/module.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/fmc.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/ioctl.h>
#include <linux/vmalloc.h>
#include <linux/list.h>
#include <linux/uaccess.h>
#include <linux/poll.h>

#include "spec-raw.h"


/* = = = = = = = = = GENERIC = = = = = = = = = = */

/*
 * sr_generic_release
 * It releases the module
 */
static int sr_generic_release(struct inode *ino, struct file *f)
{
	struct sr_instance *sr = f->private_data;

	module_put(sr->fmc->owner);

	return 0;
}


/*
 * sr_generic_open
 * It finds out in our local list which instance correspond to the given minor
 */
static int sr_generic_open(struct inode *ino, struct file *f)
{
	struct sr_instance *sr;
	int minor = iminor(ino);
	int found = 0;

	list_for_each_entry(sr, &sr_devices, list) {
		if (sr->ual.m_reg.minor == minor ||
		    sr->ual.m_dma.minor == minor ||
		    sr->ual.m_irq.minor == minor) {
			found = 1;
			break;
		}
	}

	if (!found)
		return -ENODEV;

	if (try_module_get(sr->fmc->owner) == 0)
		return -ENODEV;

	f->private_data = sr;
	return 0;
}


/* = = = = = = = = = DMA = = = = = = = = = = */


/*
 * sr_dma_poll
 * It allows user-space operation only when DMA is over. It returns 0 when
 * a DMA transfer is running.
 */
static unsigned int sr_dma_poll(struct file *f,
				struct poll_table_struct *w)
{
	struct sr_instance *sr = f->private_data;

	poll_wait(f, &sr->q_dma, w);

	if (sr_is_dma_over(sr)) {
		return POLLIN | POLLRDNORM;
	}
	return 0;
}


/*
 * sr_dma_write
 * It copies the user buffer into the local DMA buffer. The whole function
 * is protected by the DMA mutex because we need to protect the buffer during
 * the transfer. But, also the transfer length calculation because it can
 * change.
 */
static ssize_t sr_dma_write(struct file *f, const char __user *buf, size_t count,
			loff_t *offp)
{
	struct sr_instance *sr = f->private_data;
	void *ptr;

	mutex_lock(&sr->mtx);
	/* If the user is pointing outside the buffer, reset the pointer */
	if (*offp >= sr->dma.length)
		*offp = 0;

	if (count > sr->dma.length)
		count = sr->dma.length;

	if (*offp + count > sr->dma.length)
		count = sr->dma.length - *offp;

	ptr = sr->ual.dma_buf + *offp;
	if (copy_from_user(ptr, buf, count)) {
		mutex_unlock(&sr->mtx);
		return -EFAULT;
	}

	*offp += count;
	mutex_unlock(&sr->mtx);
	return count;
}


/*
 * sr_dma_read
 * It copies to user the content of the local DMA buffer. The whole function
 * is protected by the DMA mutex because we need to protect the buffer during
 * the transfer. But, also the transfer length calculation because it can
 * change
 */
static ssize_t sr_dma_read(struct file *f, char __user *buf, size_t count,
			   loff_t *offp)
{
	struct sr_instance *sr = f->private_data;
	void *ptr;

	mutex_lock(&sr->mtx);
	/* If the user is pointing outside the buffer, reset the pointer */
	if (*offp >= sr->dma.length)
		*offp = 0;

	if (count > sr->dma.length)
		count = sr->dma.length;

	if (*offp + count > sr->dma.length)
		count = sr->dma.length - *offp;

	ptr = sr->ual.dma_buf + *offp;
	if (copy_to_user(buf, ptr, count)) {
		mutex_unlock(&sr->mtx);
		return -EFAULT;
	}

	*offp += count;
	mutex_unlock(&sr->mtx);
	return count;
}


/*
 * sr_dma_ioctl_req
 * It requests a DMA transfer
 */
static int sr_dma_ioctl_req(struct sr_instance *sr, void __user *uarg)
{
	struct sr_dma_request dma;
	int err = 0, size_changed = 0;

	/*
	 * DMA work only when a DMA engine is detected and we have
	 * DMA interrupts
	 */
	if (!sr->dma_base_addr || !sr->irq_dma_base_addr) {
		dev_err(&sr->fmc->dev, "No DMA engine found on FPGA\n");
		return -EPERM;
	}

	/*
	 * Get from userspace the DMA transfer configuration and store
	 * it locally
	 */
	err = copy_from_user(&dma, uarg, sizeof(struct sr_dma_request));
	if (err)
		return err;
	if (!dma.length || dma.dev_mem_off + dma.length > SR_DDR_SIZE)
		return -EINVAL;

	/* Protect DMA transfer from concurrent request */
	mutex_lock(&sr->mtx);
	if (dma.length != sr->dma.length)
		size_changed = 1;
	memcpy(&sr->dma, &dma, sizeof(struct sr_dma_request));
	dev_dbg(&sr->fmc->dev, "%s:%d off 0x%lx  len %lu flags %lu\n",
		__func__, __LINE__,
		dma.dev_mem_off, dma.length, dma.flags);

	if (sr->dma.flags & SR_IOCTL_DMA_FLAG_WRITE) {
	/* Buffer must be there before performing write */
		if (!sr->ual.dma_buf) {
			dev_err(&sr->fmc->dev,
				"Cannot write buffer, read it before\n");
			err = -EPERM;
			goto out;
		}
	} else if (size_changed) {
	/*
	 * Release previous buffer and allocate a new one on read
	 * (only if diff size)
	 */

		/* We cannot run a DMA acquisition while someone is handling
		 * data with mmap */
		if (atomic_read(&sr->ual.map_count)) {
			dev_err(&sr->fmc->dev,
				"Cannot resize buffer while active mmap\n");
			return -EBUSY;
		}

		if (sr->ual.dma_buf)
			vfree(sr->ual.dma_buf);
		sr->ual.dma_buf = vmalloc(sr->dma.length);
		if (!sr->ual.dma_buf) {
			err = -ENOMEM;
			goto out;
		}
	}

	err = sr_dma_start(sr);
	if (err) {
		dev_err(&sr->fmc->dev, "Cannot start DMA\n");
		goto out;
	}

	/* Wait until DMA is over */
	dev_dbg(&sr->fmc->dev, "Wait for DMA over\n");

out:
	mutex_unlock(&sr->mtx);
	return err;
}


/*
 * sr_dma_ioctl
 * It handles DMA ioctl requests from user
 */
static int sr_dma_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct sr_instance *sr = f->private_data;
	void __user *uarg = (void __user *)arg;
	int err = 0;

	/* Check type and command number */
	if (_IOC_TYPE(cmd) != SR_IOCTL_MAGIC)
		return -ENOTTY;

	/* Validate user pointer */
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE, uarg, _IOC_SIZE(cmd));
	if (_IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok(VERIFY_READ, uarg, _IOC_SIZE(cmd));
	if (err)
		return -EFAULT;

	switch (cmd) {
	case SR_IOCTL_DMA:
		err = sr_dma_ioctl_req(sr, uarg);
		break;
	default:
		return -EINVAL;
	}

	return err;
}


/*
 * sr_vm_open
 * It increase the number of active users of the mmap
 */
static void sr_vm_open(struct vm_area_struct *vma)
{
	struct file *f = vma->vm_file;
	struct sr_instance *sr = f->private_data;

	atomic_inc(&sr->ual.map_count);
}


/*
 * sr_vm_close
 * It decrease the number of active users of the mmap
 */
static void sr_vm_close(struct vm_area_struct *vma)
{
	struct file *f = vma->vm_file;
	struct sr_instance *sr = f->private_data;

	atomic_dec(&sr->ual.map_count);
}


/*
 * sr_vm_fault
 * After a page fault, it maps the correct page
 */
static int sr_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct file *f = vma->vm_file;
	struct sr_instance *sr = f->private_data;
	long off = vmf->pgoff * PAGE_SIZE;
	struct page *p;
	void *addr;

	if (off > sr->dma.length)
		return VM_FAULT_SIGBUS;

	addr = sr->ual.dma_buf + off;
	p = vmalloc_to_page(addr);
	get_page(p);
	vmf->page = p;

	return 0;
}

static const struct vm_operations_struct sr_vma_ops = {
	.open = sr_vm_open,
	.close = sr_vm_close,
	.fault = sr_vm_fault,
};


/*
 * sr_dma_mmap
 * It assigns out set of virtual memory operation
 */
static int sr_dma_mmap(struct file *f, struct vm_area_struct *vma)
{
	struct sr_instance *sr = f->private_data;

	if (!sr->dma.length || !sr->ual.dma_buf)
		return -EINVAL;

	vma->vm_ops = &sr_vma_ops;
	if (vma->vm_ops->open)
		vma->vm_ops->open(vma);

	return 0;
}

const struct file_operations sr_dma_fops = {
	.owner = THIS_MODULE,
	.open = sr_generic_open,
	.release = sr_generic_release,
	.read = sr_dma_read,
	.write = sr_dma_write,
	.poll = sr_dma_poll,
	.mmap = sr_dma_mmap,
	.unlocked_ioctl = sr_dma_ioctl,
};


/* = = = = = = = = = REG = = = = = = = = = = */


/* read and write are simple after the default llseek has been used */
static ssize_t sr_read(struct file *f, char __user *buf, size_t count,
		       loff_t *offp)
{
	struct sr_instance *sr = f->private_data;
	struct fmc_device *fmc = sr->fmc;
	unsigned long addr;
	uint32_t val;

	if (count < sizeof(val))
		return -EINVAL;
	count = sizeof(val);

	addr = *offp;
	if (addr > fmc->memlen)
		return -ESPIPE; /* Illegal seek */

	val = fmc_readl(fmc, addr);
	dev_dbg(&fmc->dev, "reading address = 0x%08lx value = 0x%04x\n",
			addr, val);
	if (copy_to_user(buf, &val, count))
		return -EFAULT;
	*offp += count;
	return count;
}

static ssize_t sr_write(struct file *f, const char __user *buf, size_t count,
			loff_t *offp)
{
	struct sr_instance *sr = f->private_data;
	struct fmc_device *fmc = sr->fmc;
	unsigned long addr;
	uint32_t val;

	if (count < sizeof(val))
		return -EINVAL;
	count = sizeof(val);

	addr = *offp;
	if (addr > fmc->memlen)
		return -ESPIPE; /* Illegal seek */
	if (copy_from_user(&val, buf, count))
		return -EFAULT;
	dev_dbg(&fmc->dev, "writing address = 0x%08lx value = 0x%04x\n",
			addr, val);
	fmc_writel(fmc, val, addr);
	*offp += count;
	return count;
}

const struct file_operations sr_reg_fops = {
	.owner = THIS_MODULE,
	.open = sr_generic_open,
	.release = sr_generic_release,
	.llseek = generic_file_llseek,
	.read = sr_read,
	.write = sr_write,
};


/* = = = = = = = = = IRQ = = = = = = = = = = */


/*
 * sr_irq_read
 * It returns IRQ descriptors of the last N events
 */
static ssize_t sr_irq_read(struct file *f, char __user *buf, size_t count,
			   loff_t *offp)
{
	struct sr_instance *sr = f->private_data;
	int n, n_req;

	if (count % sizeof(struct ual_irq_status))
		return -EINVAL;

	spin_lock(&sr->ual.irq_lock);
	n = sr->ual.w_idx_irq - sr->ual.r_idx_irq;
	if (!n) /* no available irq */
		return 0;
	if (sr->ual.r_idx_irq > sr->ual.w_idx_irq) /* write restarted */
		n = UAL_IRQ_HISTORY - sr->ual.r_idx_irq;

	/* Calculate new buffer length */
	n_req = count / sizeof(struct ual_irq_status);
	n = min(n_req, n);
	count = sizeof(struct ual_irq_status) * n;

	if (copy_to_user(buf, sr->ual.last_irqs, count)) {
		spin_unlock(&sr->ual.irq_lock);
		return -EFAULT;
	}

	sr->ual.r_idx_irq += n;
	if (unlikely(sr->ual.r_idx_irq >= UAL_IRQ_HISTORY))
		sr->ual.r_idx_irq = 0;
	spin_unlock(&sr->ual.irq_lock);

	return count;
}


/*
 * sr_irq_poll
 * It allows users to read IRQ descriptor when there is at last one descriptor
 */
static unsigned int sr_irq_poll(struct file *f,
				struct poll_table_struct *w)
{
	struct sr_instance *sr = f->private_data;
	int n;

	poll_wait(f, &sr->ual.q_irq, w);

	/* check if there are interrupts to notify */
	n = sr->ual.r_idx_irq - sr->ual.w_idx_irq;
	if (n)
		return POLLIN | POLLRDNORM;
	return 0;
}

const struct file_operations sr_irq_fops = {
	.owner = THIS_MODULE,
	.open = sr_generic_open,
	.release = sr_generic_release,
	.read = sr_irq_read,
	.poll = sr_irq_poll,
};
