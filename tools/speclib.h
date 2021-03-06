#ifndef __SPECLIB_H
#define __SPECLIB_H

#include <stdint.h>

/* Vendor/Device ID to identify the SPEC */
#define PCI_VENDOR_ID_CERN	0x10dc
#define PCI_DEVICE_ID_SPEC_45T	0x018d
#define PCI_DEVICE_ID_SPEC_100T	0x01a2
#define PCI_VENDOR_ID_GENNUM	0x1a39
#define PCI_DEVICE_ID_GN4124	0x0004

/* For compatibility */
#define PCI_DEVICE_ID_SPEC PCI_DEVICE_ID_SPEC_45T

/* 'Opens' the SPEC card at PCI bus [bus], device/function [dev].
    Returns a handle to the card or NULL in case of failure. */
void *spec_open(int bus, int dev);

/* Closes the SPEC handle [card] */
void spec_close(void *card);

/* Loads the FPGA bitstream into card [card] from file [filename]. 
   Returns 0 on success. */
int spec_load_bitstream(void *card, const char *filename);

/* Load the FPGA bitstram into car [card] from a given buffer [buf]
   of size [size] */
int spec_load_bitstream_buffer(void *card, void *buf, size_t size);

/* Loads the WRC LM32 firmware into card [card] from file [filename]. starting at 
   address [base_addr]. Returns 0 on success. 
   WARNING: using improper base address/FPGA firmware will freeze the computer. */
int spec_load_lm32(void *card, const char *filename, uint32_t base_addr);

/* Raw I/O to BAR4 (Wishbone) */
void spec_writel(void *card, uint32_t data, uint32_t addr);
uint32_t spec_readl(void *card, uint32_t addr);

/* Initializes a virtual UART at base address [base_addr]. */
int spec_vuart_init(void *card, uint32_t base_addr);

/* Virtual uart Rx (VUART->Host) and Tx (Host->VUART) functions */
size_t spec_vuart_rx(void *card, char *buffer, size_t size);
size_t spec_vuart_tx(void *card, char *buffer, size_t size);

/* Get the pointer to access SPEC memory directly */
void *spec_get_base(void *card, int basenr);

enum {
	BASE_BAR0 = 0,	/* for wrpc etc (but lm32 is at 0x80000 offset) */
	BASE_BAR2 = 2,
	BASE_BAR4 = 4	/* for gennum-internal registers */
};

/* libspec version string */
extern const char * const libspec_version_s;
#endif
