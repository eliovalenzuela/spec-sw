/*
 * Copyright CERN 2014
 * Author: Federico Vaga <federico.vaga@cern.ch>
 */

#ifndef SPEC_RAW_USER_H_
#define SPEC_RAW_USER_H_

#define SR_IOCTL_DMA_FLAG_WRITE (1 << 0)

struct sr_dma_request {
	unsigned long int dev_mem_off;
	unsigned long int length;
	unsigned long int flags;
};

#define SR_IOCTL_MAGIC 's'
#define SR_IOCTL_DMA _IOWR(SR_IOCTL_MAGIC, 1, struct sr_dma_request)

#endif /* SPEC_RAW_USER_H_ */
