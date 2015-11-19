/*
 * Copyright (C) 2010-2012 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 *
 * This work is part of the White Rabbit project, a research effort led
 * by CERN, the European Institute for Nuclear Research.
 */
#ifndef __SPEC_H__
#define __SPEC_H__
#include <linux/miscdevice.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/completion.h>
#include <linux/fmc.h>
#include <linux/gpio.h>

#define PCI_VENDOR_ID_CERN	0x10dc
#define PCI_DEVICE_ID_SPEC_45T	0x018d
#define PCI_DEVICE_ID_SPEC_100T	0x01a2
#define PCI_VENDOR_ID_GENNUM	0x1a39
#define PCI_DEVICE_ID_GN4124	0x0004

#define SPEC_DEFAULT_LM32_ADDR 0x80000 /* used if "1" is passed */

#define SPEC_NAME_LEN 10

/* Our device structure */
struct spec_dev {
	struct pci_dev		*pdev;
	struct resource		*area[3];	/* bar 0, 2, 4 */
	void __iomem		*remap[3];	/* ioremap of bar 0, 2, 4 */
	unsigned long		flags;		/* see below */
	struct list_head	list;
	struct fmc_device	*fmc;
	int			irq_count;	/* for mezzanine use too */
	struct completion	compl;
	struct gpio_chip	*gpio;
	struct vic_irq_controller *vic;
	spinlock_t		irq_lock;
	struct miscdevice       mdev;

	char                    name[SPEC_NAME_LEN];
};

#define SPEC_FLAG_FAKE_EEPROM		0x00000001
#define SPEC_FLAG_IRQS_REQUESTED	0x00000002
#define SPEC_FLAG_FMC_REGISTERED	0x00000004

/* Registers for GN4124 access */
enum {
	/* page 106 */
	GNPPCI_MSI_CONTROL	= 0x48,		/* actually, 3 smaller regs */
	GNPPCI_MSI_ADDRESS_LOW	= 0x4c,
	GNPPCI_MSI_ADDRESS_HIGH	= 0x50,
	GNPPCI_MSI_DATA		= 0x54,

	GNPCI_SYS_CFG_SYSTEM	= 0x800,

	/* page 130 ff */
	GNINT_CTRL		= 0x810,
	GNINT_STAT		= 0x814,
	GNINT_CFG_0		= 0x820,
	GNINT_CFG_1		= 0x824,
	GNINT_CFG_2		= 0x828,
	GNINT_CFG_3		= 0x82c,
	GNINT_CFG_4		= 0x830,
	GNINT_CFG_5		= 0x834,
	GNINT_CFG_6		= 0x838,
	GNINT_CFG_7		= 0x83c,
#define GNINT_CFG(x) (GNINT_CFG_0 + 4 * (x))

	/* page 146 ff */
	GNGPIO_BASE = 0xA00,
	GNGPIO_BYPASS_MODE	= GNGPIO_BASE,
	GNGPIO_DIRECTION_MODE	= GNGPIO_BASE + 0x04, /* 0 == output */
	GNGPIO_OUTPUT_ENABLE	= GNGPIO_BASE + 0x08,
	GNGPIO_OUTPUT_VALUE	= GNGPIO_BASE + 0x0C,
	GNGPIO_INPUT_VALUE	= GNGPIO_BASE + 0x10,
	GNGPIO_INT_MASK		= GNGPIO_BASE + 0x14, /* 1 == disabled */
	GNGPIO_INT_MASK_CLR	= GNGPIO_BASE + 0x18, /* irq enable */
	GNGPIO_INT_MASK_SET	= GNGPIO_BASE + 0x1C, /* irq disable */
	GNGPIO_INT_STATUS	= GNGPIO_BASE + 0x20,
	GNGPIO_INT_TYPE		= GNGPIO_BASE + 0x24, /* 1 == level */
	GNGPIO_INT_VALUE	= GNGPIO_BASE + 0x28, /* 1 == high/rise */
	GNGPIO_INT_ON_ANY	= GNGPIO_BASE + 0x2C, /* both edges */

	/* page 158 ff */
	FCL_BASE		= 0xB00,
	FCL_CTRL		= FCL_BASE,
	FCL_STATUS		= FCL_BASE + 0x04,
	FCL_IODATA_IN		= FCL_BASE + 0x08,
	FCL_IODATA_OUT		= FCL_BASE + 0x0C,
	FCL_EN			= FCL_BASE + 0x10,
	FCL_TIMER_0		= FCL_BASE + 0x14,
	FCL_TIMER_1		= FCL_BASE + 0x18,
	FCL_CLK_DIV		= FCL_BASE + 0x1C,
	FCL_IRQ			= FCL_BASE + 0x20,
	FCL_TIMER_CTRL		= FCL_BASE + 0x24,
	FCL_IM			= FCL_BASE + 0x28,
	FCL_TIMER2_0		= FCL_BASE + 0x2C,
	FCL_TIMER2_1		= FCL_BASE + 0x30,
	FCL_DBG_STS		= FCL_BASE + 0x34,

	FCL_FIFO		= 0xE00,

	PCI_SYS_CFG_SYSTEM	= 0x800
};

/* Access gennum registers in a "standard" way */
static inline uint32_t gennum_readl(struct spec_dev *spec, int reg)
{
	return readl(spec->remap[2] + reg);
}
static inline void gennum_writel(struct spec_dev *spec, uint32_t val, int reg)
{
	writel(val, spec->remap[2] + reg);
}
static inline void gennum_mask_val(struct spec_dev *spec,
				   uint32_t mask, uint32_t val, int reg)
{
	uint32_t v = gennum_readl(spec, reg);
	v &= ~mask;
	v |= val;
	gennum_writel(spec, v, reg);
}

/* Functions and data in spec-pci.c */
extern int spec_load_fpga(struct spec_dev *spec, const void *data, int size);
extern int spec_load_fpga_file(struct spec_dev *spec, char *name);
extern char *spec_fw_name;
extern int spec_use_msi;

/* Functions in spec-fmc.c, used by spec-pci.c */
extern int spec_fmc_create(struct spec_dev *spec, struct fmc_gateware *gw);
extern void spec_fmc_destroy(struct spec_dev *spec);

/* Functions in spec-i2c.c, used by spec-fmc.c */
extern int spec_i2c_init(struct fmc_device *fmc);
extern void spec_i2c_exit(struct fmc_device *fmc);
extern int spec_eeprom_read(struct fmc_device *fmc, uint32_t offset,
			    void *buf, size_t size);
extern int spec_eeprom_write(struct fmc_device *fmc, uint32_t offset,
			     const void *buf, size_t size);

/* The eeprom is at address 0x50 */
#define SPEC_I2C_EEPROM_ADDR 0x50
#define SPEC_I2C_EEPROM_SIZE ((size_t)(8 * 1024))

/* Functions in spec-gpio.c */
extern int spec_gpio_init(struct fmc_device *fmc);
extern void spec_gpio_exit(struct fmc_device *fmc);

/* Functions in spec-vic.c */
/* NOTE: these functions must be called while holding irq_lock */
int spec_vic_irq_request(struct spec_dev *spec, struct fmc_device *fmc,
			 unsigned long id, irq_handler_t handler);
void spec_vic_irq_free(struct spec_dev *spec, unsigned long id);
irqreturn_t spec_vic_irq_dispatch(struct spec_dev *spec);
extern void spec_vic_irq_ack(struct spec_dev *spec, unsigned long id);
extern int vic_is_managed(struct vic_irq_controller *vic, unsigned long id);

#endif /* __SPEC_H__ */
