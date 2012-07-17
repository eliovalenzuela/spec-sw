/*
 * I2C access (on-board EEPROM)
 *
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2 as published by the Free Software Foundation or, at your
 * option, any later version.
 */
#include <linux/moduleparam.h>
#include <linux/io.h>
#include <linux/time.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include "spec.h"
#include "hw/fd_main_regs.h"

/* The eeprom is at address 0x50 */
#define I2C_ADDR 0x50
#define I2C_SIZE (8 * 1024)

static inline uint32_t spec_readl(struct spec_dev *spec, int off)
{
	return readl(spec->remap[0] + 0x80000 + off);
}
static inline void spec_writel(struct spec_dev *spec, uint32_t val, int off)
{
	writel(val, spec->remap[0] + 0x80000 + off);
}

/* Stupid dumping tool */
static void dumpstruct(char *name, void *ptr, int size)
{
	int i;
	unsigned char *p = ptr;

	printk("%s: (size 0x%x)\n", name, size);
	for (i = 0; i < size; ) {
		printk("%02x", p[i]);
		i++;
		printk(i & 3 ? " " : i & 0xf ? "  " : "\n");
	}
	if (i & 0xf)
		printk("\n");
}

static void set_sda(struct spec_dev *spec, int val)
{
	uint32_t reg;

	reg = spec_readl(spec, FD_REG_I2CR) & ~FD_I2CR_SDA_OUT;
	if (val)
		reg |= FD_I2CR_SDA_OUT;
	spec_writel(spec, reg, FD_REG_I2CR);
}

static void set_scl(struct spec_dev *spec, int val)
{
	uint32_t reg;

	reg = spec_readl(spec, FD_REG_I2CR) & ~FD_I2CR_SCL_OUT;
	if (val)
		reg |= FD_I2CR_SCL_OUT;
	spec_writel(spec, reg, FD_REG_I2CR);
}

static int get_sda(struct spec_dev *spec)
{
	return spec_readl(spec, FD_REG_I2CR) & FD_I2CR_SDA_IN ? 1 : 0;
};

static void mi2c_start(struct spec_dev *spec)
{
	set_sda(spec, 0);
	set_scl(spec, 0);
}

static void mi2c_stop(struct spec_dev *spec)
{
	set_sda(spec, 0);
	set_scl(spec, 1);
	set_sda(spec, 1);
}

int mi2c_put_byte(struct spec_dev *spec, int data)
{
	int i;
	int ack;

	for (i = 0; i < 8; i++, data<<=1) {
		set_sda(spec, data & 0x80);
		set_scl(spec, 1);
		set_scl(spec, 0);
	}

	set_sda(spec, 1);
	set_scl(spec, 1);

	ack = get_sda(spec);

	set_scl(spec, 0);
	set_sda(spec, 0);

	return ack ? -EIO : 0; /* ack low == success */
}

int mi2c_get_byte(struct spec_dev *spec, unsigned char *data, int sendack)
{
	int i;
	int indata = 0;

	/* assert: scl is low */
	set_scl(spec, 0);
	set_sda(spec, 1);
	for (i = 0; i < 8; i++) {
		set_scl(spec, 1);
		indata <<= 1;
		if (get_sda(spec))
			indata |= 0x01;
		set_scl(spec, 0);
	}

	set_sda(spec, (sendack ? 0 : 1));
	set_scl(spec, 1);
	set_scl(spec, 0);
	set_sda(spec, 0);

	*data= indata;
	return 0;
}

void mi2c_init(struct spec_dev *spec)
{
	set_scl(spec, 1);
	set_sda(spec, 1);
}

void mi2c_scan(struct spec_dev *spec)
{
	int i;
	for(i = 0; i < 256; i += 2) {
		mi2c_start(spec);
		if(!mi2c_put_byte(spec, i))
			pr_info("%s: Found i2c device at 0x%x\n",
			       KBUILD_MODNAME, i >> 1);
		mi2c_stop(spec);
	}
}

/* FIXME: this is very inefficient: read several bytes in a row instead */
int spec_eeprom_read(struct spec_dev *spec, int i2c_addr, uint32_t offset,
		void *buf, size_t size)
{
	int i;
	uint8_t *buf8 = buf;
	unsigned char c;

	for(i = 0; i < size; i++) {
		mi2c_start(spec);
		if(mi2c_put_byte(spec, i2c_addr << 1) < 0) {
			mi2c_stop(spec);
			return -EIO;
		}

		mi2c_put_byte(spec, (offset >> 8) & 0xff);
		mi2c_put_byte(spec, offset & 0xff);
		offset++;
		mi2c_stop(spec);
		mi2c_start(spec);
		mi2c_put_byte(spec, (i2c_addr << 1) | 1);
		mi2c_get_byte(spec, &c, 0);
		*buf8++ = c;
		mi2c_stop(spec);
	}
	return size;
}

int spec_eeprom_write(struct spec_dev *spec, int i2c_addr, uint32_t offset,
		 void *buf, size_t size)
{
	int i, busy;
	uint8_t *buf8 = buf;

	for(i = 0; i < size; i++) {
		mi2c_start(spec);

		if(mi2c_put_byte(spec, i2c_addr << 1) < 0) {
			mi2c_stop(spec);
			return -1;
		}
		mi2c_put_byte(spec, (offset >> 8) & 0xff);
		mi2c_put_byte(spec, offset & 0xff);
		mi2c_put_byte(spec, *buf8++);
		offset++;
		mi2c_stop(spec);

		do { /* wait until the chip becomes ready */
			mi2c_start(spec);
			busy = mi2c_put_byte(spec, i2c_addr << 1);
			mi2c_stop(spec);
		} while(busy);
	}
	return size;
}

int spec_i2c_init(struct fmc_device *fmc)
{
	struct spec_dev *spec = fmc->carrier_data;
	void *buf;
	int i;

	mi2c_scan(spec);

	buf = kmalloc(I2C_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	i = spec_eeprom_read(spec, I2C_ADDR, 0, buf, I2C_SIZE);
	if (i != I2C_SIZE) {
		dev_err(&spec->pdev->dev, "EEPROM read error: retval is %i\n",
			i);
		kfree(buf);
		return -EIO;
	}
	fmc->eeprom = buf;
	fmc->eeprom_len = I2C_SIZE;

	dumpstruct("eeprom", buf, I2C_SIZE);

	return 0;
}

void spec_i2c_exit(struct fmc_device *fmc)
{
	kfree(fmc->eeprom);
	fmc->eeprom = NULL;
	fmc->eeprom_len = 0;
}

