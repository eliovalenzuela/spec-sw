/*
 * Copyright CERN 2014
 * Author: Federico Vaga <federico.vaga@cern.ch>
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <getopt.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <string.h>

#include <spec-raw/spec-raw-user.h>

void srdd_help()
{
	fprintf(stderr, "spec-dma-dump [options] <file>\n");
	fprintf(stderr, "-l <num>: buffer byte length\n");
	fprintf(stderr, "-o 0x<hex>: offset in device memory in HEX format\n");
	fprintf(stderr, "-f 0x<hex>: bit mask of flags in HEX format\n");
	fprintf(stderr, "-h      : print this help\n");
	fprintf(stderr, "\n\n");
	fprintf(stderr, "FLAGS\n");
	fprintf(stderr, "    bit 0 : 1 for write, 0 for read\n");
	exit(1);
}

int main (int argc, char *argv[])
{
	struct sr_dma_request dma = {0, 0, 0};
	int i, fd, ret, err;
	char c, *device;
	void *map;
	uint8_t *data;

	while( (c = getopt(argc, argv, "hl:o:f:")) >=0 ){
		switch (c) {
		case '?':
		case 'h':
			srdd_help();
			break;
		case 'l':
			ret = sscanf(optarg, "%li", &dma.length);
			if (!ret)
				srdd_help();
			break;
		case 'o':
			ret = sscanf(optarg, "0x%lx", &dma.dev_mem_off);
			if (!ret)
				srdd_help();
			break;
		case 'f':
			ret = sscanf(optarg, "0x%lx", &dma.flags);
			if (!ret)
				srdd_help();
			break;
		}
	}

	if (optind != argc - 1 )
		srdd_help();

	device = argv[optind];
	fprintf(stdout, "[srdd] open device %s\n", device);

	fd = open(device, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "[srdd] cannot open device %s\n", device);
		fprintf(stderr, "       %s\n", strerror(errno));
		exit(errno);
	}

	fprintf(stdout, "[srdd] configure DMA transfer\n");
	fprintf(stdout, "       off 0x%lx len %lu flags %lu\n",
			dma.dev_mem_off, dma.length, dma.flags);
	err = ioctl(fd, SR_IOCTL_DMA, &dma);
	if (err < 0) {
		fprintf(stderr, "[srdd] cannot configure DMA %s\n", device);
		fprintf(stderr, "       %s\n", strerror(errno));
		goto out;
	}

	map = mmap(0, dma.length, PROT_READ, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		fprintf(stderr, "[srdd] cannot mmap memory\n");
		fprintf(stderr, "       %s\n", strerror(errno));
		goto out;
	}

	fprintf(stdout, "[srdd] DDR Memory content:\n");
	data = map;
	for (i = 0; i < dma.length; i += 4)
		fprintf(stdout, "0x%08lx    %02x %02x  %02x %02x\n",
			dma.dev_mem_off + i, data[i], data[i + 1],
			data[i + 2], data[i + 3]);

	munmap(map, dma.length);
out:
	close(fd);
	exit(0);
}
