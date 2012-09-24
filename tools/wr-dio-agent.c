/*
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * Released to the public domain as sample code to be customized.
 *
 * This work is part of the White Rabbit project, a research effort led
 * by CERN, the European Institute for Nuclear Research.
 */

/* Typical use: "wr-dio-agent wr1" */

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

#define RULER_PROTO 0x5752 /* WR */

/*
 * Lazily, use global variables, so the code has less parameter passing.
 * Everything in this file is using "agent_" as a prefix, to ease the
 * reader -- and remember that <TAB> aligns at 8 spaces, not 4.
 */
char			*agent_prgname;
int			agent_sock;
char			*agent_ifname;
struct ifreq		agent_ifr;


/* Boring network stuff extracted from main function */
static int agent_open_wr_sock(char *name)
{
	struct sockaddr_ll addr;
	int sock, ifindex;

	sock = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (sock < 0) {
		fprintf(stderr, "%s: socket(): %s\n",
			agent_prgname, strerror(errno));
		return -1;
	}

	memset(&agent_ifr, 0, sizeof(agent_ifr));
	strncpy(agent_ifr.ifr_name, name, sizeof(agent_ifr.ifr_name));
	if (ioctl(sock, PRIV_MEZZANINE_ID, &agent_ifr) < 0
	    /* EAGAIN is special: it means we have no ID to check yet */
		&& errno != EAGAIN) {
		fprintf(stderr, "%s: ioctl(PRIV_MEZZANINE_ID(%s)): %s\n",
			agent_prgname, name, strerror(errno));
		close(sock);
		return -1;
	}

	/* Retieve the interfaceindex */
	if (ioctl(sock, SIOCGIFINDEX, &agent_ifr) < 0) {
		fprintf(stderr, "%s: SIOCGIFINDEX(%s): %s\n", agent_prgname,
				name, strerror(errno));
		close(sock);
		return -1;
	}
	ifindex = agent_ifr.ifr_ifindex;

	/* Bind to the interface, so to be able to receive */
	memset(&addr, 0, sizeof(addr));
	addr.sll_family = AF_PACKET;
	addr.sll_protocol = htons(RULER_PROTO);
	addr.sll_ifindex = ifindex;
	addr.sll_pkttype = PACKET_BROADCAST; /* that's what ruler sends */
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "%s: bind(%s): %s\n", agent_prgname,
				name, strerror(errno));
		close(sock);
		return -1;
	}

	/* save in globals for later */
	agent_sock = sock;
	return 0;
}

/* And a simple main with the loop inside */
int main(int argc, char **argv)
{
	int len;
	/* We are receiving stuff in this frame */
	static struct frame {
		struct ether_header h;
		unsigned char pad[2];
		struct wr_dio_cmd cmd;
	} f;

	if (argc != 2) {
		fprintf(stderr, "%s: Use \"%s <wr-if>\"\n",
			argv[0], argv[0]);
		exit(1);
	}
	agent_prgname = argv[0];

	/* All functions print error messages by themselves, so just exit */
	if (agent_open_wr_sock(argv[1]) < 0)
		exit(1);

	while (1) {
		len = recv(agent_sock, &f, sizeof(f), MSG_TRUNC);
		if (len != sizeof(f)) {
			fprintf(stderr, "%s: recevied unexpected frame length"
				" (%i instead of %i)\n", agent_prgname, len,
				sizeof(f));
			continue;
		}
		if (ntohs(f.h.ether_type) != RULER_PROTO) {
			fprintf(stderr, "%s: received unexpected eth type"
				" (%04x instead of %04x)\n", agent_prgname,
				ntohs(f.h.ether_type), RULER_PROTO);
			continue;
		}

		if (0)
			printf("command %i, ch %i, t %li.%09li\n",
			       f.cmd.command, f.cmd.channel, f.cmd.t[0].tv_sec,
			       f.cmd.t[0].tv_nsec);

		/* Then simply pass it to the hardware */
		agent_ifr.ifr_data = (void *)&f.cmd;
		if (ioctl(agent_sock, PRIV_MEZZANINE_CMD, &agent_ifr) < 0) {
			fprintf(stderr, "%s: ioctl(PRIV_MEZZANINE_CMD(%s)): "
				"%s\n",	agent_prgname, agent_ifname,
				strerror(errno));
			return -1;
		}
	}
	exit(0);
}
