#ifndef __WR_NIC_H__
#define __WR_NIC_H__
#include <linux/gpio.h>

/*
 * This is the memory map of this beast, from "./top/spec/wr_nic_sdb_top.vhd"
 * in the wr-nic ohwr project (git@ohwr.org:white-rabbit/wr-nic.git for us
 * and git://ohwr.org/white-rabbit/wr-nic.git for everybody)
 *
 * -- Memory map:
 * --  0x00000000: WRPC
 * --     0x00000: WRPC I/D Memory
 * --     0x20000: WRPC Peripheral interconnect
 * --      +0x000: WRPC Minic
 * --      +0x100: WRPC Endpoint
 * --      +0x200: WRPC Softpll
 * --      +0x300: WRPC PPS gen
 * --      +0x400: WRPC Syscon
 * --      +0x500: WRPC UART
 * --      +0x600: WRPC OneWire
 * --      +0x700: WRPC Auxillary space (Etherbone config, etc)
 * --  0x00040000: WRSW NIC
 * --  0x00060000: VIC
 * --  0x00061000: TxTSU
 * --  0x00062000: DIO
 * --       0x000: DIO-ONEWIRE
 * --       0x100: DIO-I2C
 * --       0x200: DIO-GPIO
 * --       0x300: DIO-REGISTERS
 * (plus, at 63000 there are the sdb records)
 */

#define WRN_GPIO	0x62200 /* "standard" GPIO registers */
#define WRN_DIO		0x62300 /* time-aware gpio registers */
#define WRN_SDB		0x63000


#define WRN_GATEWARE_DEFAULT_NAME "fmc/wr_nic_dio.bin"

struct wrn_drvdata {
	struct gpio_chip *gc;
	struct net_device *eth;
	/* We also need the various base addresses here for fmc_writel/readl */
	__iomem void *gpio_baseaddr;
};

/* wr-nic-eth.c */
extern int wrn_eth_init(struct fmc_device *fmc);
extern void wrn_eth_exit(struct fmc_device *fmc);

/* wr-nic-gpio.h */
extern int wrn_gpio_init(struct fmc_device *fmc);
extern void wrn_gpio_exit(struct fmc_device *fmc);

#endif /* __WR_NIC_H__ */
