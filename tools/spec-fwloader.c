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
	void *map_base;
	char *fname;
	
	FILE *f;
	

	while ((c = getopt (argc, argv, "b:d:")) != -1)
	{
		switch(c)
		{
		case 'b':
			sscanf(optarg, "%i", &bus);
			break;
		case 'd':
			sscanf(optarg, "%i", &dev_fn);
			break;
		default:
			fprintf(stderr,
				"Use: \"%s [-b bus] [-d devfn] <fpga_bitstream.bin>\"\n", argv[0]);
			fprintf(stderr,
				"By default, the first available SPEC is used.\n");
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

	map_base = spec_map_area(bus, dev_fn, BASE_BAR4, 0x1000);
	if(!map_base)
	{
		fprintf(stderr, "%s: can't map the SPEC @ %02x:%02x\n", argv[0], bus, dev_fn);
		exit(1);
	}

	loader_low_level(0, map_base, buf, stbuf.st_size);

	exit (0);
}
