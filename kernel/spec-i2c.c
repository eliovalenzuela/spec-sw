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
#include <linux/delay.h>
#include <linux/fmc.h>
#include "spec.h"
#include "hw/wrc_syscon_regs.h"


static int spec_i2c_dump;
module_param_named(i2c_dump, spec_i2c_dump, int, 0444);

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

static void set_sda(struct fmc_device *fmc, int val)
{
	if (val)
		fmc_writel(fmc, SYSC_GPSR_FMC_SDA, SYSC_REG_GPSR);
	else
		fmc_writel(fmc, SYSC_GPCR_FMC_SDA, SYSC_REG_GPCR);
	ndelay(1250); /* 400kHz -> 2.5us/loop */
}

static void set_scl(struct fmc_device *fmc, int val)
{
	if (val)
		fmc_writel(fmc, SYSC_GPSR_FMC_SCL, SYSC_REG_GPSR);
	else
		fmc_writel(fmc, SYSC_GPCR_FMC_SCL, SYSC_REG_GPCR);
	ndelay(1250); /* 400kHz -> 2.5us/loop */
}

static int get_sda(struct fmc_device *fmc)
{
	return fmc_readl(fmc, SYSC_REG_GPSR) & SYSC_GPSR_FMC_SDA ? 1 : 0;
};

static void mi2c_start(struct fmc_device *fmc)
{
	set_sda(fmc, 0);
	set_scl(fmc, 0);
}

static void mi2c_stop(struct fmc_device *fmc)
{
	set_sda(fmc, 0);
	set_scl(fmc, 1);
	set_sda(fmc, 1);
}

static int mi2c_put_byte(struct fmc_device *fmc, int data)
{
	int i;
	int ack;

	for (i = 0; i < 8; i++, data<<=1) {
		set_sda(fmc, data & 0x80);
		set_scl(fmc, 1);
		set_scl(fmc, 0);
	}

	set_sda(fmc, 1);
	set_scl(fmc, 1);

	ack = get_sda(fmc);

	set_scl(fmc, 0);
	set_sda(fmc, 0);

	return ack ? -EIO : 0; /* ack low == success */
}

static int mi2c_get_byte(struct fmc_device *fmc, unsigned char *data, int ack)
{
	int i;
	int indata = 0;

	/* assert: scl is low */
	set_scl(fmc, 0);
	set_sda(fmc, 1);
	for (i = 0; i < 8; i++) {
		set_scl(fmc, 1);
		indata <<= 1;
		if (get_sda(fmc))
			indata |= 0x01;
		set_scl(fmc, 0);
	}

	set_sda(fmc, (ack ? 0 : 1));
	set_scl(fmc, 1);
	set_scl(fmc, 0);
	set_sda(fmc, 0);

	*data= indata;
	return 0;
}

void mi2c_init(struct fmc_device *fmc)
{
	set_scl(fmc, 1);
	set_sda(fmc, 1);
}

int mi2c_scan(struct fmc_device *fmc)
{
	int found = 0;

	/* ensure the bus is reset */
	mi2c_start(fmc);
	mi2c_stop(fmc);

	/* only look for our own device */
	mi2c_start(fmc);
	if(mi2c_put_byte(fmc,  fmc->eeprom_addr << 1) == 0)
		found++;
	mi2c_stop(fmc);
	return found;
}

int spec_eeprom_read(struct fmc_device *fmc, uint32_t offset,
		void *buf, size_t size)
{
	int ret = size;
	uint8_t *buf8 = buf;
	unsigned char c;

	if (offset > SPEC_I2C_EEPROM_SIZE)
		return -EINVAL;
	if (offset + size > SPEC_I2C_EEPROM_SIZE)
		return -EINVAL;

	/* Read it all in a single loop: hardware allows it */
	mi2c_start(fmc);
	if(mi2c_put_byte(fmc, fmc->eeprom_addr << 1) < 0) {
		mi2c_stop(fmc);
		return -EIO;
	}
	mi2c_put_byte(fmc, (offset >> 8) & 0xff);
	mi2c_put_byte(fmc, offset & 0xff);
	mi2c_stop(fmc);
	mi2c_start(fmc);
	mi2c_put_byte(fmc, (fmc->eeprom_addr << 1) | 1);
	while (size--) {
		mi2c_get_byte(fmc, &c, size != 0);
		*buf8++ = c;
		//printk("read 0x%08x, %4i to go\n", c, size);
	}
	mi2c_stop(fmc);
	return ret;
}

int spec_eeprom_write(struct fmc_device *fmc, uint32_t offset,
		 const void *buf, size_t size)
{
	int i, busy;
	const uint8_t *buf8 = buf;

	for(i = 0; i < size; i++) {
		mi2c_start((fmc));

		if(mi2c_put_byte(fmc, fmc->eeprom_addr << 1) < 0) {
			mi2c_stop(fmc);
			return -1;
		}
		mi2c_put_byte(fmc, (offset >> 8) & 0xff);
		mi2c_put_byte(fmc, offset & 0xff);
		mi2c_put_byte(fmc, *buf8++);
		offset++;
		mi2c_stop(fmc);

		do { /* wait until the chip becomes ready */
			mi2c_start(fmc);
			busy = mi2c_put_byte(fmc, fmc->eeprom_addr << 1);
			mi2c_stop(fmc);
		} while(busy);
	}
	return size;
}

int spec_i2c_init(struct fmc_device *fmc)
{
	struct spec_dev *spec = fmc->carrier_data;
	void *buf;
	int i, found;

	found = mi2c_scan(fmc);
	if (!found) {
		fmc->flags |= FMC_DEVICE_NO_MEZZANINE;
		return 0;
	}

	buf = kmalloc(SPEC_I2C_EEPROM_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	i = spec_eeprom_read(fmc, 0, buf, SPEC_I2C_EEPROM_SIZE);
	if (i != SPEC_I2C_EEPROM_SIZE) {
		dev_err(&spec->pdev->dev, "EEPROM read error %i\n", i);
		kfree(buf);
		fmc->eeprom = NULL;
		fmc->eeprom_len = 0;
		return -EIO;
	}
	fmc->eeprom = buf;
	fmc->eeprom_len = SPEC_I2C_EEPROM_SIZE;

	if (spec_i2c_dump)
		dumpstruct("eeprom", buf, SPEC_I2C_EEPROM_SIZE);

	return 0;
}

void spec_i2c_exit(struct fmc_device *fmc)
{
	kfree(fmc->eeprom);
	fmc->eeprom = NULL;
	fmc->eeprom_len = 0;
}

