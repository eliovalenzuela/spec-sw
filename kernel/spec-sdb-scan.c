/*
 * Copyright (C) 2019 Seven Solutions (sevensols.com)
 * Author: Miguel Jimenez Lopez <miguel.jimenez@sevensols.com>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 *
 */

#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/fmc.h>
#include <linux/fmc-sdb.h>

#include "spec.h"

/*
 * SDB information for several FPGA IP blocks.
 */
// CERN devices
// Vendor
#define SDB_CERN_VENDOR		0x0000ce42
// Products
#define SDB_NIC_PID			0x00000012
#define SDB_NIC_NAME		"NIC"
#define SDB_EP_PID			0x650c2d4f
#define SDB_EP_NAME			"EP"
#define SDB_TXTSU_PID		0x00000014
#define SDB_TXTSU_NAME		"TxTSU"
#define SDB_PPSG_PID		0xde0d8ced
#define SDB_PPSG_NAME		"PPSG"
#define SDB_VIC_PID			0x00000013
#define SDB_VIC_NAME		"VIC"
#define SDB_GPIO_PID		0x441c5143
#define SDB_GPIO_NAME		"GPIO"
#define SDB_RAM_PID			0x66cfeb52
#define SDB_RAM_NAME		"RAM"
#define SDB_SYSCON_PID		0xff07fc47
#define SDB_SYSCON_NAME		"SYSCON"
// Seven Solutions devices
// Vendor
#define SDB_7SOLS_VENDOR	0x000075cb
// Products
#define SDB_DIO_PID			0x00000001
#define SDB_DIO_NAME		"DIO"

/**
 * @brief FPGA IP block structure.
 *
 * This contains basic information in regards of a single FPGA
 * IP block.
 */
struct fpga_ip {
	char *name; /**< Name of the FPGA block*/
	uint64_t vid; /**< Vendor identifier for the FPGA block */
	uint32_t pid; /**< Product identifier for the FPGA block */
	unsigned int irq; /**< FPGA block interrupt if any */
	int has_irq; /**< FPGA block has interrupt? **/
};

// 10 FPGA cores is the maximum value for a single FPGA device
#define FPGA_DEV_MAX_CORES 10
/**
 * @brief FPGA device structure.
 *
 * This contains information regarding FPGA blocks that are
 * required by a specific FPGA device.
 */
struct fpga_dev {
	char *name; /**< Name of the FPGA device */
	struct fpga_ip cores[FPGA_DEV_MAX_CORES]; /**< List of cores required by the FPGA device */
	unsigned int n_cores; /**< Number of elements in the cores list **/

	struct platform_device dev; /**< Linux platform device for the FPGA device **/
	int dev_registered; /**< Linux platform device has been registered into system **/
};

/**
 * Release function for the FPGA platform device.
 *
 *@param dev Linux device for FPGA block
 *
 *@warning It does not require any specific application by now.
 */
static void fpga_dev_release(struct device *dev){}

// Macros for creating a static fpga_dev array
#define FPGA_DEVS_CREATE_BEGIN_DEV(id,name_dev) \
	[id] = {\
		.name = name_dev, \
		.cores = {
#define FPGA_DEVS_CREATE_END_DEV(n_elements,name_pdev,release_pdev) \
	}, \
	.n_cores = n_elements, \
	.dev = { \
		.name = name_pdev, \
		.id = 0, \
		.dev.release = release_pdev \
		} \
	},
#define FPGA_DEV_CREATE_FULL(id,name_dev,vendor_id,product_id,fpga_irq,fpga_has_irq) \
	[id] = {.name = name_dev, \
	.vid = vendor_id, \
	.pid = product_id,\
	.irq = fpga_irq, \
	.has_irq = fpga_has_irq},
	
#define FPGA_DEV_CREATE_SFULL(id,name_dev,vendor_id,product_id,fpga_irq) \
	[id] = {.name = name_dev, \
	.vid = vendor_id, \
	.pid = product_id,\
	.irq = fpga_irq, \
	.has_irq = 1},

#define FPGA_DEV_CREATE(id,name_dev,vendor_id,product_id) \
	FPGA_DEV_CREATE_FULL(id,name_dev,vendor_id,product_id,0,0)

// NIC FPGA device
#define FPGA_DEVS_NIC_DEV 0
#define FPGA_DEVS_NIC_DEV_NAME "spec-nic"
#define FPGA_DEVS_NIC_DEV_NIC 0
#define FPGA_DEVS_NIC_DEV_NIC_IRQ 1
#define FPGA_DEVS_NIC_DEV_EP 1
#define FPGA_DEVS_NIC_DEV_TXTSU 2
#define FPGA_DEVS_NIC_DEV_TXTSU_IRQ 0
#define FPGA_DEVS_NIC_DEV_PPSG 3
#define FPGA_DEVS_NIC_DEV_NUM 4
#define FPGA_DEVS_NIC_DEV_PDEV_NAME "spec-nic" // FIXME: Match with the NIC driver
#define FPGA_DEVS_NIC_DEV_PDEV_RELEASE_F fpga_dev_release
// VIC FPGA device
#define FPGA_DEVS_VIC_DEV 1
#define FPGA_DEVS_VIC_DEV_NAME "spec-vic"
#define FPGA_DEVS_VIC_DEV_VIC 0
#define FPGA_DEVS_VIC_DEV_NUM 1
#define FPGA_DEVS_VIC_DEV_PDEV_NAME "htvic-spec"
#define FPGA_DEVS_VIC_DEV_PDEV_RELEASE_F fpga_dev_release
// DIO FPGA device
#define FPGA_DEVS_DIO_DEV 2
#define FPGA_DEVS_DIO_DEV_NAME "spec-fmc-dio"
#define FPGA_DEVS_DIO_DEV_DIO 0
#define FPGA_DEVS_DIO_DEV_DIO_IRQ 2
#define FPGA_DEVS_DIO_DEV_GPIO 1
#define FPGA_DEVS_DIO_DEV_PPSG 2
//#define FPGA_DEVS_DIO_DEV_RAM 3
//#define FPGA_DEVS_DIO_DEV_SYSCON 4
#define FPGA_DEVS_DIO_DEV_NUM 3 //5
#define FPGA_DEVS_DIO_DEV_PDEV_NAME "fmc-dio-spec"
#define FPGA_DEVS_DIO_DEV_PDEV_RELEASE_F fpga_dev_release

// Global structure for FPGA devices
static struct fpga_dev fpga_devs[] =
{
		/* NIC */
		FPGA_DEVS_CREATE_BEGIN_DEV(FPGA_DEVS_NIC_DEV,FPGA_DEVS_NIC_DEV_NAME)
		FPGA_DEV_CREATE_SFULL(FPGA_DEVS_NIC_DEV_NIC,SDB_NIC_NAME,SDB_CERN_VENDOR,SDB_NIC_PID,FPGA_DEVS_NIC_DEV_NIC_IRQ)
		FPGA_DEV_CREATE(FPGA_DEVS_NIC_DEV_EP,SDB_EP_NAME,SDB_CERN_VENDOR,SDB_EP_PID)
		FPGA_DEV_CREATE_SFULL(FPGA_DEVS_NIC_DEV_TXTSU,SDB_TXTSU_NAME,SDB_CERN_VENDOR,SDB_TXTSU_PID,FPGA_DEVS_NIC_DEV_TXTSU_IRQ)
		FPGA_DEV_CREATE(FPGA_DEVS_NIC_DEV_PPSG,SDB_PPSG_NAME,SDB_CERN_VENDOR,SDB_PPSG_PID)
		FPGA_DEVS_CREATE_END_DEV(FPGA_DEVS_NIC_DEV_NUM,FPGA_DEVS_NIC_DEV_PDEV_NAME,FPGA_DEVS_NIC_DEV_PDEV_RELEASE_F)
		/* VIC */
		FPGA_DEVS_CREATE_BEGIN_DEV(FPGA_DEVS_VIC_DEV,FPGA_DEVS_VIC_DEV_NAME)
		FPGA_DEV_CREATE(FPGA_DEVS_VIC_DEV_VIC,SDB_VIC_NAME,SDB_CERN_VENDOR,SDB_VIC_PID)
		//FPGA_DEV_CREATE_SFULL(FPGA_DEVS_VIC_DEV_VIC,SDB_VIC_NAME,SDB_CERN_VENDOR,SDB_VIC_PID,FPGA_DEVS_VIC_DEV_VIC_IRQ) // EYYYYY!!
		FPGA_DEVS_CREATE_END_DEV(FPGA_DEVS_VIC_DEV_NUM,FPGA_DEVS_VIC_DEV_PDEV_NAME,FPGA_DEVS_VIC_DEV_PDEV_RELEASE_F)
		/* DIO */
		FPGA_DEVS_CREATE_BEGIN_DEV(FPGA_DEVS_DIO_DEV,FPGA_DEVS_DIO_DEV_NAME)
		FPGA_DEV_CREATE_SFULL(FPGA_DEVS_DIO_DEV_DIO,SDB_DIO_NAME,SDB_7SOLS_VENDOR,SDB_DIO_PID,FPGA_DEVS_DIO_DEV_DIO_IRQ)
		FPGA_DEV_CREATE(FPGA_DEVS_DIO_DEV_GPIO,SDB_GPIO_NAME,SDB_CERN_VENDOR,SDB_GPIO_PID)
		FPGA_DEV_CREATE(FPGA_DEVS_DIO_DEV_PPSG,SDB_PPSG_NAME,SDB_CERN_VENDOR,SDB_PPSG_PID)
		//FPGA_DEV_CREATE(FPGA_DEVS_DIO_DEV_RAM,SDB_RAM_NAME,SDB_CERN_VENDOR,SDB_RAM_PID)
		//FPGA_DEV_CREATE(FPGA_DEVS_DIO_DEV_RAM,SDB_SYSCON_NAME,SDB_CERN_VENDOR,SDB_SYSCON_PID)
		FPGA_DEVS_CREATE_END_DEV(FPGA_DEVS_DIO_DEV_NUM,FPGA_DEVS_DIO_DEV_PDEV_NAME,FPGA_DEVS_DIO_DEV_PDEV_RELEASE_F)

};

/**
 * Register a Linux platform device for a specific FPGA device.
 *
 * This function reads information of the FPGA device, creates resources for each specific FPGA cores
 * and registers the Linux platform device.
 *
 * @param fmc FMC device
 * @param fdev FPGA device
 *
 * @return 0 if success and a negative error code otherwise
 */
static int spec_sdb_fpga_dev_register(struct fmc_device *fmc, struct fpga_dev *fdev)
{
	signed long start;
	unsigned long size;
	struct resource *res = NULL;
	struct spec_dev *spec = fmc->carrier_data;
	unsigned int irq = spec->pdev->irq;
	int i, j, r;
	unsigned int n_irqs = 0;
	unsigned int n_res = 0;

	if(fdev->n_cores <= 0 || fdev->n_cores >= FPGA_DEV_MAX_CORES)
		return -EINVAL;

	// Count the number of interrupts for the FPGA blocks
	for(i = 0 ; i < fdev->n_cores ; i++) {
		// If FPGA block is VIC pass the IRQ info for SPEC PCIe
		if(fdev->cores[i].vid == SDB_CERN_VENDOR && fdev->cores[i].pid == SDB_VIC_PID) {
			fdev->cores[i].irq = irq;
			fdev->cores[i].has_irq = 1;
		}

		if(fdev->cores[i].has_irq)
			n_irqs++;
	}
	
	n_res = fdev->n_cores+n_irqs;
	
	res = kzalloc(sizeof(*res)*n_res, GFP_KERNEL);
	if(!res)
		return -ENOMEM;

	for(i = 0, j = 0 ; i < fdev->n_cores ; i++) {
		start = fmc_find_sdb_device(fmc->sdb,
				fdev->cores[i].vid,
				fdev->cores[i].pid,&size);
		if(start < 0) {
			kfree(res);
			return -EINVAL;
		}

		res[j].name = fdev->cores[i].name;
		res[j].flags = IORESOURCE_MEM;
		res[j].start = spec->area[0]->start+start;
		res[j].end = res[j].start+size-1;

#if 0
		printk("Res[%d]: %pR Parent: %p\n",j,&res[j],res[j].parent);
#endif
		j++;

		if(fdev->cores[i].has_irq) {
			res[j].name = fdev->cores[i].name;
			res[j].flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL;
			res[j].start = fdev->cores[i].irq;
			res[j].end = fdev->cores[i].irq;

#if 0
			printk("Res[%d]: %pR Parent: %p\n",j,&res[j],res[j].parent);
#endif
			j++;
		}

	}

	fdev->dev.resource = res;
	fdev->dev.num_resources = fdev->n_cores+n_irqs;

	/* Set the FMC device as private data for the platform_device */
	platform_set_drvdata(&fdev->dev,fmc);

	r = platform_device_register(&fdev->dev);
	if(r) {
		fdev->dev_registered = 0;
		kfree(res);
	}
	else {
		fdev->dev_registered = 1;
	}

	return r;
}

/**
 * Register Linux platform devices for the all FPGA resources.
 *
 * @param fmc FMC device
 *
 * @return 0 if success and a negative error code otherwise
 */
int spec_sdb_fpga_dev_register_all(struct fmc_device *fmc)
{
	int i;
	int r;

	for(i = 0 ; i < ARRAY_SIZE(fpga_devs) ; i++) {
		r = spec_sdb_fpga_dev_register(fmc,&fpga_devs[i]);
		if(r != 0)
			dev_err(&fmc->dev,"%s: %s could not be registered! \n",
					__func__,fpga_devs[i].name);
	}

	return r;
}

/**
 * Release Linux platform devices for the all FPGA resources.
 *
 * @param fmc FMC device
 */
void spec_sdb_fpga_dev_release_all(struct fmc_device *fmc)
{
	int i;
	struct fpga_dev *dev;

	for(i = 0 ; i < ARRAY_SIZE(fpga_devs) ; i++) {
		dev = &fpga_devs[i];
		if(dev->dev_registered) {
			platform_device_unregister(&dev->dev);
			kfree(dev->dev.resource);
		}
	}

	return;
}
