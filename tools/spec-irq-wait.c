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
#include <poll.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <string.h>

#include <spec-raw/spec-raw-user.h>
#include <spec-raw/spec-raw-ual.h>

void srdd_help()
{
	fprintf(stderr, "spec-irq-wait [options] -D <cdev> \n");
	fprintf(stderr, "-D <cdev>: char device to use\n");
	fprintf(stderr, "-n <num>: number of interrupts to wait\n");
	fprintf(stderr, "-s <seconds> : seconds timeout\n");
	fprintf(stderr, "-u <micro seconds> : microseconds timeout\n");
	exit(1);
}

int main (int argc, char *argv[])
{
	int i, n = 1, fd, ret;
	char c, *device = NULL;
	struct timeval to = {1, 0}, to_tmp;
	fd_set rfds;
	struct ual_irq_status st;

	while( (c = getopt(argc, argv, "hD:n:s:u:")) >=0 ){
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
		case 's':
			ret = sscanf(optarg, "%li", &to.tv_sec);
			if (!ret)
				srdd_help();
			break;
		case 'u':
			ret = sscanf(optarg, "%li", &to.tv_usec);
			if (!ret)
				srdd_help();
			break;
		}
	}

	if (!device)
		srdd_help();

	fd = open(device, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "[sriw] cannot open device %s\n", device);
		fprintf(stderr, "       %s\n", strerror(errno));
		exit(errno);
	}


	for (i = 0; i < n;) {
		to_tmp = to;

		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);

		ret = select(fd + 1, &rfds, NULL, NULL, &to_tmp);
		switch (ret) {
		case 0:
			fprintf(stderr,
				"[sriw] timeout while waiting interrupt %i\n", i);
			break;
		case -1:
			fprintf(stderr, "[sriw] something wrong error %d %s\n",
				errno, strerror(errno));
			break;
		default:
			ret = read(fd, &st, sizeof(struct ual_irq_status));
			if (ret < 0) {
				fprintf(stderr, "[sriw] something wrong error %d %s\n",
					errno, strerror(errno));
				i++;
			}

			if (ret != sizeof(struct ual_irq_status)) {
				fprintf(stderr,
					"[sriw] invalid IRQ description read %d != %lu\n",
					ret, sizeof(struct ual_irq_status));
				break;
			}

		       	fprintf(stdout, "[sriw] received interrupt %i\n", i);
			fprintf(stdout, "       source: 0x%x   status 0x%x\n",
				st.source, st.status);
			i++;
			break;
		}
	}

	close(fd);
	exit(0);
}
