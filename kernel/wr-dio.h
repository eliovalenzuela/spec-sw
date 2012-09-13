/*
 * Copyright (C) 2010-2012 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 *
 * This work is part of the White Rabbit project, a research effort led
 * by CERN, the European Institute for Nuclear Research.
 */
#ifndef __WR_DIO_H__
#define __WR_DIO_H__

#include "wbgen-regs/wr-dio-regs.h"

/* This should be included by both the kernel and the tools */

enum wr_dio_cmd_name {
	WR_DIO_CMD_OUT,
	WR_DIO_CMD_STAMP,
	WR_DIO_CMD_DAC,
};

#define WR_DIO_N_STAMP  16 /* At least 5 * 3 */

struct wr_dio_cmd {
	uint16_t command;	/* from user */
	uint16_t value;		/* for DAC */
	uint32_t channel;	/* 0..4 from user */
	uint32_t flags;
	uint32_t nstamp;	/* from kernel, if IN_STAMP */
	struct timespec t[WR_DIO_N_STAMP];	/* t[0] may be from user */
};

#define WR_DIO_F_NOW	0x01	/* Output is now, t[0] ignored */
#define WR_DIO_F_REL	0x02	/* t[0].tv_sec is relative */
#define WR_DIO_F_MASK	0x04	/* Channel is 0x00..0x1f, use t[0..4,5..9] */
#define WR_DIO_F_LOOP	0x08	/* Output should loop: t[2] is  looping*/

#endif /* __WR_DIO_H__ */
