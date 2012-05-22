/*
 * A tool to program our soft-core (LM32) within the SPEC.
 *
 * Alessandro Rubini 2012 for CERN, GPLv2 or later.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "spec-tools.h"

int main(int argc, char **argv)
{
	int bus = -1, dev_fn = -1, i, c;
	struct stat stbuf;
	unsigned char *buf;
	unsigned int *ibuf;
	volatile uint32_t *p;
	void *map_base;
	char *fname;
	uint32_t lm32_base = 0x80000;
	
	FILE *f;
	

	while ((c = getopt (argc, argv, "b:d:c:")) != -1)
	{
		switch(c)
		{
		case 'b':
			sscanf(optarg, "%i", &bus);
			break;
		case 'd':
			sscanf(optarg, "%i", &dev_fn);
			break;
		case 'c':
			sscanf(optarg, "%i", &lm32_base);
			break;
		default:
			fprintf(stderr,
				"Use: \"%s [-b bus] [-d devfn] <fpga_bitstream.bin>\"\n", argv[0]);
			fprintf(stderr,
				"By default, the first available SPEC is used and the LM32 is assumet at 0x%x.\n", lm32_base);
			exit(1);
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "Expected binary name after options.\n");
		exit(1);
	}
    
	fname = argv[optind];

	f = fopen(fname, "r");
	if (!f) {
		fprintf(stderr, "%s: %s: %s\n", argv[0], fname,
			strerror(errno));
		exit(1);
	}
	if (fstat(fileno(f), &stbuf)) {
		fprintf(stderr, "%s: stat(%s): %s\n", argv[0], fname,
			strerror(errno));
		exit(1);
	}
	if (!S_ISREG(stbuf.st_mode)) {
		fprintf(stderr, "%s: %s: Not a regular file\n", argv[0],
			fname);
		exit(1);
	}

	buf = malloc(stbuf.st_size);
	if (!buf) {
		fprintf(stderr, "%s: Can't allocate buffer (%li bytes): %s\n",
			argv[0], (long)stbuf.st_size, strerror(errno));
		exit(1);
	}
	i = fread(buf, 1, stbuf.st_size, f);
	if (i < 0) {
		fprintf(stderr, "%s: read(%s): %s\n", argv[0], fname,
			strerror(errno));
		exit(1);
	}
	if (i != stbuf.st_size) {
		fprintf(stderr, "%s: short read from %s\n", argv[0], fname);
		exit(1);
	}

	map_base = spec_map_area(bus, dev_fn, BASE_BAR0, 0x100000);
	if(!map_base)
	{
		fprintf(stderr, "%s: can't map the SPEC @ %02x:%02x\n", argv[0], bus, dev_fn);
		exit(1);
	}

	ibuf = (void *)buf;

	/* Phew... we are there, finally */
	*(volatile uint32_t *)(map_base + lm32_base + 0x20400) = 0x1deadbee;
	while ( !((*(volatile uint32_t *)(map_base + lm32_base + 0x20400)) & (1<<28)) )
		;

	p = map_base + lm32_base;
	for (i = 0; i < (stbuf.st_size + 3) / 4; i++) {
		p[i] = htonl(ibuf[i]); /* big endian */
	}

	sync();

	for (i = 0; i < (stbuf.st_size + 3) / 4; i++) {
		if (p[i] != htonl(ibuf[i]))
			fprintf(stderr, "programming error at %x "
				"(expected %08x, found %08x)\n", i*4,
				htonl(ibuf[i]), p[i]);
	}

	sync();

	*(volatile uint32_t *)(map_base + lm32_base + 0x20400) = 0x0deadbee;

	if (getenv("VERBOSE"))
		printf("%s: Wrote %li bytes at offset 0x%x\n", argv[0],
		       (long)stbuf.st_size, lm32_base);
	exit (0);
}
