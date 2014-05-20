/*
 * Copyright (C) 2014 CERN (www.cern.ch)
 * Author  : Federico Vaga <federico.vaga@cern.ch>
 * License : GPL version 2 or later 
 */

#define UAL_IRQ_HISTORY 32
#define UAL_IRQ_MAX_SUBSCRIPTION 8

/*
 * ual_irq_status
 * @source : id of the source of interrupt
 * @status : interrupt status register
 * @offset : offset from the base address of the interrupt status register
 */
struct ual_irq_status {
	uint32_t source;
	uint32_t status;
	uint32_t offset;
};

#ifdef __KERNEL__
/*
 * ual_device
 * @m_reg: char device to access device memory to R/W registers
 *         f_op->read  : read a value in the memory
 *         f_op->write : write a value in the memory
 *         f_op->llseek: move memory pointer
 * @m_dma: char device to access DMA memory
 *         f_op->read  : read the DMA memory
 *         f_op->write : write the DMA memory
 *         f_op->ioctl : request DMA transfer
 *         f_op->mmap  : read/write DMA buffer
 *         f_op->poll  : check if new data is ready, or output buffer is empty
 *         sysfs dma_direction [r/w] : the direction of the transfer
 *         sysfs dma_length [r/w] : how many bytes transfer on DMA
 *         sysfs dma_offset [r/w] : device memory offset where start DMA
 *         sysfs dma_automove_offset [r/w] : if 1 it update the offset after
 *                 each transfer
 * @m_irq: char device that provide information about the interrupts 
 *         f_op->poll  : it returns when an interrupt occur
 *         f_op->read  : it returns the last occurred interrupts
 *         sysfs irq_event_request [r/w]: on write it requests to be notified
 *                 when an interrupt occurs. On read it returns the list of
 *                 all requested interrupt events;
 * @q_dma: it is the wait queue for DMA buffer. Used by poll_wait() waiting
 *         for DMA transfer done. 
 * @q_irq: it is the wait queue for interrupt. Used by poll_wait() waiting
 *         for interrupt, and waken up when any interrupt occur
 * @last_irqs: array content the history of the IRQs (circular buffer)
 * @r_idx_irq: read index of the history array
 * @w_idx_irq: write intex of the history array
 * @dma_buf: DMA buffer
 */
struct ual_device {
	struct miscdevice m_reg;
	struct miscdevice m_dma;
	struct miscdevice m_irq;

	struct attribute_group dma;
	struct attribute_group irq;

	wait_queue_head_t q_dma;
	wait_queue_head_t q_irq;
	
	struct ual_irq_status subscribed_irqs[UAL_IRQ_MAX_SUBSCRIPTION];
	struct ual_irq_status last_irqs[UAL_IRQ_HISTORY];
	unsigned int r_idx_irq;
	unsigned int w_idx_irq;
	spinlock_t irq_lock;

	void *dma_buf;
	atomic_t map_count;

	void *priv;
};

static inline void sr_irq_status_clean(struct ual_irq_status *st)
{
	st->source = 0xBADC0FFE;
	st->status = 0x0;
	st->offset = 0x0;
}
#endif
