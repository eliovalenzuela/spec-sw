/*
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * Released to the public domain as sample code to be customized.
 *
 * This work is part of the White Rabbit project, a research effort led
 * by CERN, the European Institute for Nuclear Research.
 */

/* Typical use: "wr-dio-ruler wr1 IN0 L3+0.001 R3+0.001 L4+0.002" */

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

static char git_version[] = "version: " GIT_VERSION;

#define RULER_PROTO 0x5752 /* WR */
/*
 * Lazily, use global variables, so the code has less parameter passing.
 * Everything in this file is using "ruler_" as a prefix, to ease the
 * reader -- and remember that <TAB> aligns at 8 spaces, not 4.
 */
char			*ruler_prgname;
int			ruler_sock;
char			*ruler_ifname;
struct wr_dio_cmd	ruler_cmd;
struct ifreq		ruler_ifr;
unsigned char		ruler_macaddr[ETH_ALEN];

struct ruler_action {
	int isremote;
	int channel;
	struct timespec delay;
};

/* Boring parsing separated to a separate function (same code as elsewhere) */
static int parse_ts(char *s, struct timespec *ts)
{
	int i, n;
	unsigned long nano;
	char c;

	/*
 	 * Hairy: if we scan "%ld%lf", the 0.009999 will become 9998 micro.
	 * Thus, scan as integer and string, so we can count leading zeros
	 */

	nano = 0;
	ts->tv_sec = 0;
	ts->tv_nsec = 0;

	if ( (i = sscanf(s, "%ld.%ld%c", &ts->tv_sec, &nano, &c)) == 1)
		return 0; /* seconds only */
	if (i == 3)
		return -1; /* trailing crap */
	if (i == 0)
		if (sscanf(s, ".%ld%c", &nano, &c) != 1)
			return -1; /* leading or trailing crap */

	s = strchr(s, '.') + 1;
	n = strlen(s);
	if (n > 9)
		return -1; /* too many decimals */
	while (n < 9) {
		nano *= 10;
		n++;
	}
	ts->tv_nsec = nano;
	return 0;
}


/* Boring network stuff extracted from main function */
static int ruler_open_wr_sock(char *name)
{
	struct sockaddr_ll addr;
	int sock, ifindex;

	sock = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (sock < 0) {
		fprintf(stderr, "%s: socket(): %s\n",
			ruler_prgname, strerror(errno));
		return -1;
	}

	memset(&ruler_ifr, 0, sizeof(ruler_ifr));
	strncpy(ruler_ifr.ifr_name, name, sizeof(ruler_ifr.ifr_name));
	if (ioctl(sock, PRIV_MEZZANINE_ID, &ruler_ifr) < 0
	    /* EAGAIN is special: it means we have no ID to check yet */
		&& errno != EAGAIN) {
		fprintf(stderr, "%s: ioctl(PRIV_MEZZANINE_ID(%s)): %s\n",
			ruler_prgname, name, strerror(errno));
		close(sock);
		return -1;
	}

	/* Retrieve the local MAC address to send correct Ethernet frames */
	if (ioctl(sock, SIOCGIFHWADDR, &ruler_ifr) < 0) {
		fprintf(stderr, "%s: SIOCGIFHWADDR(%s): %s\n", ruler_prgname,
			name, strerror(errno));
		close(sock);
		return -1;
	}
	memcpy(ruler_macaddr, ruler_ifr.ifr_hwaddr.sa_data,
	       sizeof(ruler_macaddr));

	/* Retieve the interfaceindex */
	if (ioctl(sock, SIOCGIFINDEX, &ruler_ifr) < 0) {
		fprintf(stderr, "%s: SIOCGIFINDEX(%s): %s\n", ruler_prgname,
				name, strerror(errno));
		close(sock);
		return -1;
	}
	ifindex = ruler_ifr.ifr_ifindex;

	/* Bind to the interface, so to be able to send */
	memset(&addr, 0, sizeof(addr));
	addr.sll_family = AF_PACKET;
	addr.sll_protocol = htons(RULER_PROTO);
	addr.sll_ifindex = ifindex;
	addr.sll_pkttype = PACKET_OUTGOING;
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "%s: bind(%s): %s\n", ruler_prgname,
				name, strerror(errno));
		close(sock);
		return -1;
	}

	/* save in globals for later */
	ruler_sock = sock;
	ruler_ifname = name;
	return 0;
}

/* The channel being monitored will be configured as input */
static int ruler_config_in(char *chname)
{
	int ch;
	char c;

	if (sscanf(chname, "IN%i%c", &ch, &c) != 1) {
		fprintf(stderr, "%s: Argument \"%s\" must be \"IN<ch>\"\n",
			ruler_prgname, chname);
		return -1;
	}
	if (ch < 0 || ch > 4) {
		fprintf(stderr, "%s: Out of range channel number in \"%s\"\n",
			ruler_prgname, chname);
		return -1;
	}
	return ch;
}

/* Actions are allocated in an array and returned for later use */
static struct ruler_action *ruler_build_actions(int nact, char **strings)
{
	struct ruler_action *act;
	char *s;
	int i, ch;
	char lr, c;

	act = calloc(nact, sizeof(*act));
	if (!act) {
		fprintf(stderr, "%s: %s\n", ruler_prgname, strerror(errno));
		return NULL;
	}
	for (i = 0; i < nact; i++) {
		if (sscanf(strings[i], "%c%i+%c\n", &lr, &ch, &c) != 3) {
			fprintf(stderr, "%s: Wrong argument \"%s\"\n",
			       ruler_prgname, strings[i]);
			free(act);
			return NULL;
		}
		if (lr != 'L' && lr != 'R') {
			fprintf(stderr, "%s: Wrong argument \"%s\"\n",
			       ruler_prgname, strings[i]);
			free(act);
			return NULL;
		}
		if (ch < 0 || ch > 4) {
			fprintf(stderr, "%s: Out of range channel in \"%s\"\n",
			       ruler_prgname, strings[i]);
			free(act);
			return NULL;
		}
		s = strchr(strings[i], '+') + 1;
		if (parse_ts(s, &act[i].delay) < 0) {
			fprintf(stderr, "%s: Wrong time \"%s\" in \"%s\"\n",
			       ruler_prgname, s, strings[i]);
			free(act);
			return NULL;
		}

		if (lr == 'L')
			act[i].isremote = 0;
		else
			act[i].isremote = 1;
		act[i].channel = ch;
	}

	for (i = 0; i < nact; i++) {
		fprintf(stderr, "%s: configured for %s channel %i, "
			"delay %li.%09li\n", ruler_prgname,
			act[i].isremote ? "remote" : " local",
			act[i].channel,
			act[i].delay.tv_sec, act[i].delay.tv_nsec);
	}
	return act;
}

/* The main loop will wait for an event... */
static int ruler_wait_event(int inch, struct timespec *ts)
{
	ruler_cmd.command = WR_DIO_CMD_STAMP;
	ruler_cmd.flags = WR_DIO_F_WAIT;
	ruler_cmd.channel = inch;

	ruler_ifr.ifr_data = (void *)&ruler_cmd;
	if (ioctl(ruler_sock, PRIV_MEZZANINE_CMD, &ruler_ifr) < 0 ) {
		fprintf(stderr, "%s: ioctl(PRIV_MEZZANINE_CMD(%s)): "
			"%s\n", ruler_prgname, ruler_ifname, strerror(errno));
		return -1;
	}
	/* Assume it's only one stamp */
	*ts = ruler_cmd.t[0];
	return 0;
}

/* ...and run all actions when the event happens */
static int ruler_run_actions(int nact, struct timespec *ts,
			     struct ruler_action *actions)
{
	int i;

	/* We are building stuff in this frame, to possibly send it */
	static struct frame {
		struct ether_header h;
		unsigned char pad[2];
		struct wr_dio_cmd cmd;
	} f;

	/* Most parameters are unchanged over actions */
	memset(&f.h.ether_dhost, 0xff, ETH_ALEN); /* broadcast */
	memcpy(&f.h.ether_shost, ruler_macaddr, ETH_ALEN);
	f.h.ether_type = ntohs(RULER_PROTO);
	f.cmd.t[1].tv_nsec = 1000 * 1000; /* 1ms*/

	f.cmd.command = WR_DIO_CMD_PULSE;

	for (i = 0; i < nact; i++) {
		f.cmd.channel = actions[i].channel;
		f.cmd.t[0] = *ts;
		/* add the requested delay */
		f.cmd.t[0].tv_sec += actions[i].delay.tv_sec;
		f.cmd.t[0].tv_nsec += actions[i].delay.tv_nsec;
		if (f.cmd.t[0].tv_nsec > 1000 * 1000 * 1000) {
			f.cmd.t[0].tv_nsec -= 1000 * 1000 * 1000;
			f.cmd.t[0].tv_sec++;
		}

		if (actions[i].isremote) {
			if (send(ruler_sock, &f, sizeof(f), 0) < 0) {
				fprintf(stderr, "%s: send(): %s\n",
					ruler_prgname, strerror(errno));
				return -1;
			}
			continue;
		}

		/* local */
		ruler_ifr.ifr_data = (void *)&f.cmd;
		if (ioctl(ruler_sock, PRIV_MEZZANINE_CMD, &ruler_ifr) < 0) {
			fprintf(stderr, "%s: ioctl(PRIV_MEZZANINE_CMD(%s)): "
				"%s\n",	ruler_prgname, ruler_ifname,
				strerror(errno));
			return -1;
		}
	}
	return 0;
}

static void print_version(char *pname)
{
	printf("%s %s\n", pname, git_version);
}

/* Finally, a main function to wrap it all */
int main(int argc, char **argv)
{
	struct ruler_action *actions;
	struct timespec ts;
	int inch;

	if ((argc == 2) && (!strcmp(argv[1], "-V"))) {
		print_version(argv[0]);
		exit(0);
	}

	if (argc < 4) {
		fprintf(stderr, "%s: Use \"%s [-V] <wr-if> IN<ch> "
			"{L,R}<ch>+<delay-as-decimal> [...]\n",
			argv[0], argv[0]);
		exit(1);
	}
	ruler_prgname = argv[0];

	/* All functions print error messages by themselves, so just exit */
	if (ruler_open_wr_sock(argv[1]) < 0)
		exit(1);

	inch = ruler_config_in(argv[2]);
	if (inch < 0)
		exit(1);

	actions = ruler_build_actions(argc - 3, argv + 3);
	if (!actions)
		exit(1);

	while(1) {
		if (ruler_wait_event(inch, &ts) < 0)
			exit(1);
		if (ruler_run_actions(argc - 3, &ts, actions) < 0)
			exit(1);
	}
	exit(0);
}
