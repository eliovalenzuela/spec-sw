/*
 * Copyright (C) 2014 CERN (www.cern.ch)
 * Author: Federico Vaga <federico.vaga@cern.ch>
 */
#include <linux/module.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/fmc.h>
#include <linux/fs.h>
#include <linux/random.h>

#include "spec-raw.h"

#define SR_DMA_LOOP_SIZE 512

/*
 * sr_get_user_offset
 * It returns the integer value corresponding to the offset written by the user
 */
static int sr_get_user_offset(const char __user *buf, size_t count)
{
	char tmp[16];
	int val, ret;

	if (count < 3) /* 0x0 */
		return -EINVAL;

	if (copy_from_user(tmp, buf, count))
		return -EFAULT;

	ret = sscanf(tmp, "0x%x", &val);
	if (!ret)
		return -EINVAL;

	if (val >= SR_DDR_SIZE)
		return -EINVAL;

	return val;
}



/*
 * sr_fix_transfer_length
 * It fixes the len according to the DDR dimension
 */
static inline size_t sr_fix_transfer_length(size_t len)
{
	size_t len_to_write;

	if (SR_DDR_SIZE - len > SR_DMA_LOOP_SIZE)
		len_to_write = SR_DMA_LOOP_SIZE;
	else
		len_to_write = SR_DDR_SIZE - len;

	return len_to_write;
}



/*
 * sr_write_ddr
 * @sr: spec-raw instance
 * @off: DDR offset where write data (negative number mean random)
 * @data: bytes to write
 * @len: data length
 *
 * It writes onto DDR memory using DMA. This function saves and then reuses the
 * the dma descriptor and data field in the sr instance. This is not the best
 * choice but we are in debugging mode no-one  should use these operations
 * unless for debugging purpose.
 */
static int sr_write_ddr(struct sr_instance *sr, int off,
			uint8_t *data, size_t len)
{
	struct sr_dma_request dma;
	void *old_buf;
	int err;

	/* Save previous status and clear the data ready flag */
	dma = sr->dma;
	old_buf = sr->ual.dma_buf;
	sr->ual.dma_buf = data;

	/* Setup DDR offset */
	if (off < 0)
		get_random_bytes(&sr->dma.dev_mem_off, sizeof (sr->dma.dev_mem_off) );
	else
		sr->dma.dev_mem_off = off;

	/* Configure DMA transfer */
	sr->dma.dev_mem_off &= (SR_DDR_SIZE - 1);
	sr->dma.length = len;
	sr->dma.flags = SR_IOCTL_DMA_FLAG_WRITE;

	/* Start DMA [write] and wait until complete */
	err = sr_dma_start(sr);
	if (err) {
		dev_err(&sr->fmc->dev, "Cannot start DMA\n");
		return err;
	}
	wait_event_interruptible(sr->q_dma, sr_is_dma_over(sr));

	/* DMA is over, restore previous status */
	sr->dma = dma;
	sr->ual.dma_buf = old_buf;

	return 0;
}



/*
 * sr_simple_open
 * It just configure the private data
 *
 * This function is copied from the kernel version 3.13 because it is not
 * available on 3.2 (where this module will run)
 */
static int sr_simple_open(struct inode *inode, struct file *file)
{
	if (inode->i_private)
		file->private_data = inode->i_private;
	return 0;
}



/*
 * sr_dbg_loop_read
 * It writes random bytes on the DDR memory at a random offset, then it reads
 * back those bytes to verify that read/write is working
 */
static ssize_t sr_dbg_loop_read(struct file *f, char __user *buf, size_t count,
				loff_t *offp)
{
	struct sr_instance *sr = f->private_data;
	struct sr_dma_request dma;
	void *old_buf;
	uint8_t datain[SR_DMA_LOOP_SIZE], dataout[SR_DMA_LOOP_SIZE];
	int err;
	char msg[128]= {"\0"};

	if (*offp)
		return 0;

	dev_info(&sr->fmc->dev, "Run DMA loop test\n");
	/* Fill the buffer to write */
	get_random_bytes(dataout, SR_DMA_LOOP_SIZE);

	/* Protect DMA transfers */
	mutex_lock(&sr->mtx);
	err = sr_write_ddr(sr, -1, dataout, SR_DMA_LOOP_SIZE);
	if (err)
		goto out;

	dma = sr->dma;
	old_buf = sr->ual.dma_buf;
	sr->ual.dma_buf = datain;

	sr->dma.flags &= ~SR_IOCTL_DMA_FLAG_WRITE;
	err = sr_dma_start(sr);
	if (err) {
		dev_err(&sr->fmc->dev, "Cannot start DMA\n");
		goto out;
	}
	wait_event_interruptible(sr->q_dma, sr_is_dma_over(sr));

	if (memcmp(datain, dataout, SR_DMA_LOOP_SIZE))
		sprintf(msg, "Test Failed!\n");
	else
		sprintf(msg, "Test Successful!\n");

	sr->dma = dma;
	sr->ual.dma_buf = old_buf;

	count = strlen(msg);
	if (copy_to_user(buf, msg, count)) {
		err = -EFAULT;
		goto out;
	}

	*offp += count;

out:
	mutex_unlock(&sr->mtx);
	return count;
}

const struct file_operations sr_dbgfs_dma_loop_op = {
	.owner = THIS_MODULE,
	.open  = sr_simple_open,
	.read  = sr_dbg_loop_read,
};



/*
 * sr_dbg_write_seq
 * At a given offset, it writes on the DDR memory a sequence of numbers
 */
static ssize_t sr_dbg_write_seq(struct file *f, const char __user *buf, size_t count,
				loff_t *offp)
{
	struct sr_instance *sr = f->private_data;
	uint8_t dataout[SR_DMA_LOOP_SIZE];
	uint32_t val;
	int i, err;

	if (*offp)
		return 0;

	val = sr_get_user_offset(buf, count);
	if (val < 0)
		return val;

	dev_info(&sr->fmc->dev, "Write sequence in DDR at offset 0x%x\n", val);
	/* Fill the buffer to write */
	for (i = 0; i < SR_DMA_LOOP_SIZE; ++i)
		dataout[i] = i & 0xFF;

	mutex_lock(&sr->mtx);
	err = sr_write_ddr(sr, val, dataout, sr_fix_transfer_length(val));
	mutex_unlock(&sr->mtx);
	if (err)
		return err;

	return count;
}
const struct file_operations sr_dbgfs_dma_write_seq = {
	.owner = THIS_MODULE,
	.open  = sr_simple_open,
	.write  = sr_dbg_write_seq,
};



/*
 * sr_dbg_write_zero
 * At a given offset, it writes on the DDR memory a sequence of zeros
 */
static ssize_t sr_dbg_write_zero(struct file *f, const char __user *buf,
				size_t count, loff_t *offp)
{
	struct sr_instance *sr = f->private_data;
	uint8_t dataout[SR_DMA_LOOP_SIZE];
	uint32_t val;
	int err;

	if (*offp)
		return 0;

	val = sr_get_user_offset(buf, count);
	if (val < 0)
		return val;

	dev_info(&sr->fmc->dev, "Write zero in DDR at offset 0x%x\n", val);
	/* Fill the buffer to write */
	memset(dataout, 0, SR_DMA_LOOP_SIZE);

	mutex_lock(&sr->mtx);
	err = sr_write_ddr(sr, val, dataout, sr_fix_transfer_length(val));
	mutex_unlock(&sr->mtx);
	if (err)
		return err;

	return count;
}
const struct file_operations sr_dbgfs_dma_write_zero = {
	.owner = THIS_MODULE,
	.open  = sr_simple_open,
	.write  = sr_dbg_write_zero,
};
