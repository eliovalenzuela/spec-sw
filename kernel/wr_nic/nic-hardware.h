/*
 * hardware-specific definitions for the White Rabbit NIC
 *
 * Copyright (C) 2010-2014 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __WR_NIC_HARDWARE_H__
#define __WR_NIC_HARDWARE_H__
#if (!defined WR_IS_NODE) && (!defined WR_IS_SWITCH)
#error "WR_NODE and WR_SWITCH not defined!"
#endif

#if WR_IS_SWITCH
/* This is the clock used in internal counters. */
#define REFCLK_FREQ (125000000 / 2)
#define NSEC_PER_TICK (NSEC_PER_SEC / REFCLK_FREQ)

/* The interrupt is one of those managed by our WRVIC device */
#define WRN_IRQ_BASE		192
#define WRN_IRQ_NIC		(WRN_IRQ_BASE + 0)
#define WRN_IRQ_TSTAMP		(WRN_IRQ_BASE + 1)
//#define WRN_IRQ_PPSG		(WRN_IRQ_BASE + )
//#define WRN_IRQ_RTU		(WRN_IRQ_BASE + )
//#define WRN_IRQ_RTUT		(WRN_IRQ_BASE + )

/*
 *	V3 Memory map, temporarily (Jan 2012)
 *
 *  0x00000 - 0x1ffff:    RT Subsystem
 *  0x00000 - 0x0ffff: RT Subsystem Program Memory (16 - 64 kB)
 *  0x10000 - 0x100ff: RT Subsystem UART
 *  0x10100 - 0x101ff: RT Subsystem SoftPLL-adv
 *  0x10200 - 0x102ff: RT Subsystem SPI Master
 *  0x10300 - 0x103ff: RT Subsystem GPIO
 *  0x10500 - 0x105ff: PPS gen
 *  0x20000 - 0x3ffff:     NIC
 *  0x20000 - 0x20fff  NIC control regs and descriptor area
 *  0x28000 - 0x2bfff  NIC packet buffer (16k)
 *  0x30000 - 0x4ffff:           Endpoints
 *  0x30000 + N * 0x400  Endpoint N control registers
 *  0x50000 - 0x50fff:  VIC
 *  0x51000 - 0x51fff:  Tstamp unit
 */
/* This is the base address of all the FPGA regions (EBI1, CS0) */
#define FPGA_BASE_PPSG	0x10010500
#define FPGA_SIZE_PPSG	0x00000100
#define FPGA_BASE_NIC	0x10020000
#define FPGA_SIZE_NIC	0x00010000
#define FPGA_BASE_EP	0x10030000
#define FPGA_SIZE_EP	0x00010000
#define FPGA_SIZE_EACH_EP	0x400
#define FPGA_BASE_VIC	0x10050000 /* not used here */
#define FPGA_SIZE_VIC	0x00001000
#define FPGA_BASE_TS	0x10051000
#define FPGA_SIZE_TS	0x00001000
#endif /* WR_IS_SWITCH */

#if WR_IS_NODE 
/* This is the clock used in internal counters. */
#define REFCLK_FREQ (125000000)
#define NSEC_PER_TICK (NSEC_PER_SEC / REFCLK_FREQ)

/* The interrupt is one of those managed by our WRVIC device */
#define WRN_IRQ_BASE		0 /* FIXME: relative to pci dev */
#define WRN_IRQ_NIC		(WRN_IRQ_BASE + 0)
#define WRN_IRQ_TSTAMP		/* (WRN_IRQ_BASE + 1) -- not used here */
//#define WRN_IRQ_PPSG		(WRN_IRQ_BASE + )
//#define WRN_IRQ_RTU		(WRN_IRQ_BASE + )
//#define WRN_IRQ_RTUT		(WRN_IRQ_BASE + )

/*
 *	spec-wr-nic memory map (from SDB dump):
 *
 *  00000651:e6a542c9 WB4-Crossbar-GSI
 *  0000ce42:00000011 WR-CORE             (bridge: 00000000)
 *     00000651:e6a542c9 WB4-Crossbar-GSI
 *     0000ce42:66cfeb52 WB4-BlockRAM        (00000000-00015fff)
 *     00000651:eef0b198 WB4-Bridge-GSI      (bridge: 00020000)
 *        00000651:e6a542c9 WB4-Crossbar-GSI
 *        0000ce42:ab28633a WR-Mini-NIC         (00020000-000200ff)
 *        0000ce42:650c2d4f WR-Endpoint         (00020100-000201ff)
 *        0000ce42:65158dc0 WR-Soft-PLL         (00020200-000202ff)
 *        0000ce42:de0d8ced WR-PPS-Generator    (00020300-000203ff)
 *        0000ce42:ff07fc47 WR-Periph-Syscon    (00020400-000204ff)
 *        0000ce42:e2d13d04 WR-Periph-UART      (00020500-000205ff)
 *        0000ce42:779c5443 WR-Periph-1Wire     (00020600-000206ff)
 *        0000ce42:779c5443 WR-Periph-1Wire     (00020700-000207ff)
 *  0000ce42:00000012 WR-NIC              (00040000-0005ffff)
 *  0000ce42:00000013 WB-VIC-Int.Control  (00060000-000600ff)
 *  0000ce42:00000014 WR-TXTSU            (00061000-000610ff)
 *  000075cb:00000002 WR-DIO-Core         (bridge: 00062000)
 *     00000651:e6a542c9 WB4-Crossbar-GSI
 *     0000ce42:779c5443 WR-1Wire-master     (00062000-000620ff)
 *     0000ce42:123c5443 WB-I2C-Master       (00062100-000621ff)
 *     0000ce42:441c5143 WB-GPIO-Port        (00062200-000622ff)
 *     000075cb:00000001 WR-DIO-Registers    (00062300-000623ff)
 * 
 */

/* This is the base address of memory regions (gennum bridge, bar 0) */
#define FPGA_BASE_LM32	0x00000000
#define FPGA_SIZE_LM32	0x00016000

#define FPGA_BASE_NIC	0x00020000
#define FPGA_SIZE_NIC	0x00000100

#define FPGA_BASE_EP	0x00020100
#define FPGA_SIZE_EP	0x00000100
#define FPGA_SIZE_EACH_EP	0x100 /* There is one only */

#define FPGA_BASE_PPSG	0x00020300
#define FPGA_SIZE_PPSG	0x00000100

#define FPGA_BASE_VIC	0x00060000 /* not used here */
#define FPGA_SIZE_VIC	0x00000100
#define FPGA_BASE_TS	0x00061000
#define FPGA_SIZE_TS	0x0000 100
#endif /* ifdef WR_IS_NODE */


enum fpga_blocks {
	WRN_FB_NIC,
	WRN_FB_EP,
	WRN_FB_PPSG,
	WRN_FB_TS,
	WRN_NR_OF_BLOCKS,
};

/* In addition to the above enumeration, we scan for those many endpoints */
#if WR_IS_NODE
#  define WRN_NR_ENDPOINTS		1
#endif
#if WR_IS_SWITCH
#  define WRN_NR_ENDPOINTS		18
#endif

/* 8 tx and 8 rx descriptors */
#define WRN_NR_DESC	8
#define WRN_NR_TXDESC	WRN_NR_DESC
#define WRN_NR_RXDESC	WRN_NR_DESC

/* Magic number for endpoint */
#define WRN_EP_MAGIC 0xcafebabe

/*
 * The following headers include the register lists, and have been
 * generated by wbgen from .wb source files in svn
 */
#include "../wbgen-regs/endpoint-regs.h"
#include "../wbgen-regs/ppsg-regs.h"
#include "../wbgen-regs/nic-regs.h"
#include "../wbgen-regs/tstamp-regs.h"

/*
 * To make thins easier, define the descriptor structures, for tx and rx
 * Use functions in nic-mem.h to get pointes to them
 */
struct wrn_txd {
	uint32_t tx1;
	uint32_t tx2;
	uint32_t tx3;
	uint32_t unused;
};

struct wrn_rxd {
	uint32_t rx1;
	uint32_t rx2;
	uint32_t rx3;
	uint32_t unused;
};

/* Some more constants */
#define WRN_MTU 1540

#define WRN_DDATA_OFFSET 2 /* data in descriptors is offset by that much */

#endif /* __WR_NIC_HARDWARE_H__ */
