/*
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * Released to the public domain as sample code to be customized.
 *
 * This work is part of the White Rabbit project, a research effort led
 * by CERN, the European Institute for Nuclear Research.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/if_ether.h>
#include <net/if.h>
#include <netpacket/packet.h>

#include "wr_nic/wr-nic.h"
#include "wr-dio.h"

/**
 * This takes two arguments: interface name and channel number
 *
 * This simple tools just show an example of how to program with wr-nic/dio.
 * If you want to measure WR timing we suggest to use the hardwire PPS on channel 0
 * that gives a more accurate precision.
 **/

int main(int argc, char **argv)
{
	struct wr_dio_cmd _cmd;
	struct wr_dio_cmd *cmd = &_cmd;
	struct ifreq ifr;
	char *prgname = argv[0];
	char *ifname = "wr0";
	int sock, ch, charg = 1;
	char c;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "%s: Use \"%s [<ifname>] <channel>\"\n",
			prgname, prgname);
		exit(1);
	}
	if (argc == 3) {
		ifname = argv[1];
		charg++;
	}
	if (sscanf(argv[charg], "%i%c", &ch, &c) != 1) {
		fprintf(stderr, "%s: Not a channel number \"%s\"\n",
			prgname, argv[charg]);
		exit(1);
	}
	if (ch < 1 || ch > 4) {
		fprintf(stderr, "%s: Out of range channel number \"%s\"\n",
			prgname, argv[charg]);
		exit(1);
	}

	fprintf(stderr, "%s: Using interface \"%s\" and channel %i for "
		"pps output\n", prgname, ifname, ch);


	/* The following is standard stuff to access wr-nic */

	sock = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (sock < 0) {
		fprintf(stderr, "%s: socket(): %s\n",
			prgname, strerror(errno));
		exit(1);
	}

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(sock, PRIV_MEZZANINE_ID, &ifr) < 0
	    /* EAGAIN is special: it means we have no ID to check yet */
		&& errno != EAGAIN) {
		fprintf(stderr, "%s: ioctl(PRIV_MEZZANINE_ID(%s)): %s\n",
			prgname, ifname, strerror(errno));
	}


	/* Fill the command structure */

	memset(cmd, 0, sizeof(*cmd));

	cmd->command = WR_DIO_CMD_PULSE;
	cmd->channel = ch;
	cmd->flags = WR_DIO_F_REL | WR_DIO_F_LOOP;
	/* Number of loops: -1 <=> Inf */
	cmd->value = -1;
	/* 2s delay to have time to send and process this command */
	cmd->t[0].tv_sec = 2;
	/* 1ms pulse width */
	cmd->t[1].tv_nsec = 1000 * 1000;
	/* Loop period */
	cmd->t[2].tv_sec = 1;


	/* Give it to the driver and we are done */

	ifr.ifr_data = (void *)cmd;
	if (ioctl(sock, PRIV_MEZZANINE_CMD, &ifr) < 0) {
		fprintf(stderr, "%s: ioctl(PRIV_MEZZANINE_CMD(%s)): %s\n",
			prgname, ifname, strerror(errno));
		exit(1);
	}

	exit(0);
}
