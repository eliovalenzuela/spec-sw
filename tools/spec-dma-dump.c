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
#include <sys/time.h>

#include <spec-raw/spec-raw-user.h>

void srdd_help()
{
	fprintf(stderr, "spec-dma-dump [options] -D <cdev> [DATA]\n");
	fprintf(stderr, "-D <cdev>: char device to use\n");
	fprintf(stderr, "-n <num>: number of buffer to acquire\n");
	fprintf(stderr, "-l <num>: buffer byte length\n");
	fprintf(stderr, "-o 0x<hex>: offset in device memory in HEX format\n");
	fprintf(stderr, "-f 0x<hex>: bit mask of flags in HEX format\n");
	fprintf(stderr, "-m : use mmap\n");
	fprintf(stderr, "-h      : print this help\n");
	fprintf(stderr, "[DATA]  : data to write on DDR (binary format)\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "FLAGS\n");
	fprintf(stderr, "    bit 0 : 1 for write, 0 for read\n");
	exit(1);
}

int srdd_get_datalength(int argc, char *argv[])
{
	int i, count = 0;

	for (i = optind; i < argc; ++i)
		count += strlen(argv[i]);

	return count;
}
void srdd_fill_buffer(int argc, char *argv[], uint8_t *buf, size_t buflen)
{
	int i, k, b = 0;

	/* Inspect all arguments */
	for (i = optind; i < argc; ++i)
		for (k = 0; k < strlen(argv[i]) && b < buflen; ++k, ++b)
			buf[b] = argv[i][k];
}

int dump_mmap(int fd, struct sr_dma_request *dma)
{
	uint8_t *data;
	void *map;
	int i;

	map = mmap(0, dma->length, PROT_READ, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		fprintf(stderr, "[srdd] cannot mmap memory\n");
		fprintf(stderr, "       %s\n", strerror(errno));
		return -1;
	}
	
	fprintf(stdout, "[srdd] DDR Memory content (mmap):\n");
	data = map;
	for (i = 0; i < dma->length; i += 4)
		fprintf(stdout, "0x%08lx    %02x %02x  %02x %02x\n",
			dma->dev_mem_off + i, data[i], data[i + 1],
			data[i + 2], data[i + 3]);
	
	munmap(map, dma->length);

	return 0;
}

int dump_cdev(int fd, struct sr_dma_request *dma)
{
	uint8_t *data;
	ssize_t len;
	void *map;
	int i;

	map = malloc(dma->length);
	if (!map)
		return -1;

	len = read(fd, map, dma->length);
	if (len < 0) {
		free(map);
		return -1;
	}

	fprintf(stdout, "[srdd] DDR Memory content (read):\n");
	data = map;
	for (i = 0; i < len; i += 4)
		fprintf(stdout, "0x%08lx    %02x %02x  %02x %02x\n",
			dma->dev_mem_off + i, data[i], data[i + 1],
			data[i + 2], data[i + 3]);	
	free(map);
	return 0;
}

int fill_buf_mmap(int fd, struct sr_dma_request *dma, int argc, char *argv[])
{
	uint8_t *data;
	void *map;

	map = mmap(0, dma->length, PROT_WRITE, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		fprintf(stderr, "[srdd] cannot mmap memory\n");
		fprintf(stderr, "       %s\n", strerror(errno));
		return -1;
	}

	data = map;
	/* Fill the buffer */
	srdd_fill_buffer(argc, argv, data, dma->length);
	munmap(map, dma->length);

	return 0;
}

int fill_buf_cdev(int fd, struct sr_dma_request *dma, int argc, char *argv[])
{
	uint8_t *data;
	ssize_t len;
	void *map;
	int ret = 0;

	map = malloc(dma->length);
	if (!map)
		return -1;

	data = map;
	srdd_fill_buffer(argc, argv, data, dma->length);

	len = write(fd, data, dma->length);
	if (len < 0)
		ret = -1;

	free(map);
	return ret;
}

int main (int argc, char *argv[])
{
	struct sr_dma_request dma = {0, 0, 0};
	int b, n = 1, fd, ret, err, fdflags, buflen, use_mmap = 0;
	struct timeval to = {10, 0}, to_tmp;
	char c, *device = NULL;
	fd_set rfds;

	while( (c = getopt(argc, argv, "hl:o:f:D:n:m")) >=0 ){
		switch (c) {
		case '?':
		case 'h':
			srdd_help();
			break;
		case 'n':
			ret = sscanf(optarg, "%i", &n);
			if (!ret)
				srdd_help();
			break;
		case 'D':
			device = optarg;
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
		case 'm':
			use_mmap = 1;
			break;
		}
	}

	if (!device)
		srdd_help();

	/*if (optind != argc - 1 )
		srdd_help();

	device = argv[optind];
	fprintf(stdout, "[srdd] open device %s\n", device);*/

	fdflags = (dma.flags & SR_IOCTL_DMA_FLAG_WRITE) ? O_RDWR : O_RDONLY;
	fd = open(device, fdflags);
	if (fd < 0) {
		fprintf(stderr, "[srdd] cannot open device %s\n", device);
		fprintf(stderr, "       %s\n", strerror(errno));
		exit(errno);
	}

	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);

	/* WRITE DMA */
	if (dma.flags & SR_IOCTL_DMA_FLAG_WRITE) {
		/* We must read the buffer in order to get it */
		dma.flags &= ~SR_IOCTL_DMA_FLAG_WRITE;

		/*
		 * If we have less byte than the declared length, then
		 * resize the buffer. If the user provide more byte they will
		 * be discarded
		 */
		buflen = srdd_get_datalength(argc, argv);
		if (buflen < dma.length)
			dma.length = buflen;

		err = ioctl(fd, SR_IOCTL_DMA, &dma);
		if (err < 0) {
			fprintf(stderr, "[srdd] cannot configure DMA %s\n", device);
			fprintf(stderr, "       %s\n", strerror(errno));
			goto out;
		}

		if (use_mmap)
			err = fill_buf_mmap(fd, &dma, argc, argv);
		else
			err = fill_buf_cdev(fd, &dma, argc, argv);

		if (err)
			goto out;

		/* Restore write flag */
		dma.flags |= SR_IOCTL_DMA_FLAG_WRITE;
	}


	/* READ DMA */
	fprintf(stdout, "[srdd] configure DMA transfer\n");
	fprintf(stdout, "       off 0x%lx len %lu flags %lu\n",
			dma.dev_mem_off, dma.length, dma.flags);

	/* read all buffers */
	for (b = 0; b < n; ++b) {
		/* Request DMA transfer */
		err = ioctl(fd, SR_IOCTL_DMA, &dma);
		if (err < 0) {
			fprintf(stderr, "[srdd] cannot configure DMA %s\n", device);
			fprintf(stderr, "       %s\n", strerror(errno));
			goto out;
		}

		to_tmp = to;
		ret = select(fd + 1, &rfds, NULL, NULL, &to_tmp);
		switch (ret) {
		case 0:
			fprintf(stderr, "[srdd] DMA timeout for buffer %i\n", b);
			break;
		case -1:
			fprintf(stderr, "[srdd] DMA select error - %s\n",
				strerror(errno));
			break;
		default:
			if (use_mmap)
				ret = dump_mmap(fd, &dma);
			else
				ret = dump_cdev(fd, &dma);

			if (ret) {
				fprintf(stderr, "[srdd] something wrong during dump '%s'\n",
					strerror(errno));
			}
			break;
		}

		/* update offset for the next acquisition */
		dma.dev_mem_off += dma.length;
	}
out:
	close(fd);
	exit(0);
}
