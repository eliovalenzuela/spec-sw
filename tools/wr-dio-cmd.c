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

char *prgname;
char c;
int sock;
char *ifname;
struct ifreq ifr;

struct wr_dio_cmd _cmd;
struct wr_dio_cmd *cmd = &_cmd;

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

static int scan_pulse(int argc, char **argv)
{
	char c;

	if (argc != 4 && argc != 6) {
		fprintf(stderr, "%s: %s: wrong number of arguments\n",
			prgname, argv[0]);
		fprintf(stderr, "  Use: %s <channel> <duration> <when> "
			"[<period> <count>]\n", argv[0]);
		return -1;
	}
	if (sscanf(argv[1], "%hi%c", &cmd->channel, &c) != 1
		|| cmd->channel > 4) {
		fprintf(stderr, "%s: %s: not a channel number \"%s\"\n",
			prgname, argv[0], argv[1]);
		return -1;
	}

	/* Duration is first time argument but position 1 for ioctl */
	if (parse_ts(argv[2], cmd->t + 1) < 0) {
		fprintf(stderr, "%s: %s: invalid time \"%s\"\n",
			prgname, argv[0], argv[2]);
		return -1;
	}
	if (cmd->t[1].tv_sec) {
		fprintf(stderr, "%s: %s: duration must be < 1s (got \"%s\")\n",
			prgname, argv[0], argv[2]);
		return -1;
	}

	/* Next argument is the "when", position 0 in ioctl timestamp array */
	if (!strcmp(argv[3], "now")) {
		cmd->flags |= WR_DIO_F_NOW;
	} else {
		char *s2 = argv[3];

		if (s2[0] == '+') {
			cmd->flags |= WR_DIO_F_REL;
			s2++;
		}

		if (parse_ts(s2, cmd->t) < 0) {
			fprintf(stderr, "%s: %s: invalid time \"%s\"\n",
				prgname, argv[0], argv[3]);
			return -1;
		}
	}

	/* If argc is 6, we have period and count */
	if (argc == 6) {
		cmd->flags |= WR_DIO_F_LOOP;

		if (parse_ts(argv[4], cmd->t + 2) < 0) {
			fprintf(stderr, "%s: %s: invalid time \"%s\"\n",
				prgname, argv[0], argv[4]);
			return -1;
		}
		if (sscanf(argv[5], "%i%c", &cmd->value, &c) != 1) {
			fprintf(stderr, "%s: %s: invalid count \"%s\"\n",
				prgname, argv[0], argv[5]);
			return -1;
		}
	}

	ifr.ifr_data = (void *)cmd;
	if (ioctl(sock, PRIV_MEZZANINE_CMD, &ifr) < 0) {
		fprintf(stderr, "%s: ioctl(PRIV_MEZZANINE_CMD(%s)): %s\n",
			prgname, ifname, strerror(errno));
			return -1;
	}
	return 0;
}

static int scan_stamp(int argc, char **argv, int ismask)
{
	int i, ch;
	char c;

	cmd->flags = 0;
	if (argc == 3 && !strcmp(argv[2], "wait")) {
		cmd->flags = WR_DIO_F_WAIT;
		argc = 2;
	}
	if (argc == 1) {
		ismask = 1;
		ch = 0x1f;
	} else if (argc == 2) {
		if (sscanf(argv[1], "%i%c", &ch, &c) != 1) {
			fprintf(stderr, "%s: %s: not a channel \"%s\"\n",
				prgname, argv[0], argv[1]);
			exit(1);
		}
		if (ch < 0 || ch > 31 || (!ismask && ch > 4)) {
			fprintf(stderr, "%s: %s: out of range channel \"%s\"\n",
				prgname, argv[0], argv[1]);
			exit(1);
		}
	} else {
		fprintf(stderr, "%s: %s: wrong number of arguments\n",
			prgname, argv[0]);
		if (ismask)
			fprintf(stderr, "  Use: %s [<channel-mask>]\n",
				argv[0]);
		else
			fprintf(stderr, "  Use: %s [<channel>] [wait]\n",
				argv[0]);
		return -1;
	}
	if (ismask)
		cmd->flags = WR_DIO_F_MASK;

	while (1) {
		cmd->channel = ch;
		errno = 0;
		ifr.ifr_data = (void *)cmd;
		if (ioctl(sock, PRIV_MEZZANINE_CMD, &ifr) < 0 ) {
			if (errno == EAGAIN)
				break;
			fprintf(stderr, "%s: ioctl(PRIV_MEZZANINE_CMD(%s)): "
				"%s\n", prgname, ifname, strerror(errno));
		return -1;
		}
		for (i = 0; i < cmd->nstamp; i++)
			printf("ch %i, %9li.%09li\n", cmd->channel,
			       (long)cmd->t[i].tv_sec, cmd->t[i].tv_nsec);
	}
	return 0;
}

static int one_mode(int c, int index)
{
	if (c == '-')
		return 0;
	cmd->channel |= 1 << index;

	//Add error message for channel 0 
	if(index==0 && strchr("dD01",c))
	{
		fprintf(stderr, "Error: Only p/P modes are available as ouput mode for channel 0\n");
		return -1;
	}
	
	switch(c) {
	case 'D':
		cmd->value |= WR_DIO_INOUT_TERM << index;
	case 'd':
		cmd->value |= WR_DIO_INOUT_DIO << index;
		cmd->value |= WR_DIO_INOUT_OUTPUT << index;
		break;

	case 'C':
		cmd->value |= WR_DIO_INOUT_TERM << index;
	case 'c':
		cmd->value |= WR_DIO_INOUT_DIO << index;
		cmd->value |= WR_DIO_INOUT_VALUE << index;
		if(index!=4)
			fprintf(stdout, "Warning: Clock mode is only available for last channel (ch4)\n,"
			 "(on other channel it corresponds to input mode without interruptions)\n");
		break;

	case 'P':
		cmd->value |= WR_DIO_INOUT_TERM << index;
	case 'p':
		cmd->value |= WR_DIO_INOUT_DIO << index;
		cmd->value |= WR_DIO_INOUT_VALUE << index;
		cmd->value |= WR_DIO_INOUT_OUTPUT << index;
		break;

	case 'I':
		cmd->value |= WR_DIO_INOUT_TERM << index;
	case 'i':
		break;

	case '1':
		cmd->value |= WR_DIO_INOUT_VALUE << index;
	case '0':
		cmd->value |= WR_DIO_INOUT_OUTPUT << index;
		break;

	default:
		fprintf(stderr, "%s: mode: invalid mode '%c'\n",
			prgname, c);
		return -1;
	}
	return 0;
}


static int scan_inout(int argc, char **argv)
{
	int i, ch;
	char c;

	cmd->flags = WR_DIO_F_MASK;
	cmd->channel = 0;
	cmd->value = 0;

	if (argc == 2) {
		if (strlen(argv[1]) != 5) {
			fprintf(stderr, "%s: %s: wrong argument \"%s\"\n",
				prgname, argv[0], argv[1]);
			exit(1);
		}
		for (i = 0; i < 5; i++)
			if (one_mode(argv[1][i], i) < 0)
				return -1;
	} else {
		if (argc < 3 || argc > 11 || ((argc & 1) == 0)) {
			fprintf(stderr, "%s: %s: wrong number of arguments\n",
				prgname, argv[0]);
			return -1;
		}
		while (argc >= 3) {
			if (sscanf(argv[1], "%i%c", &ch, &c) != 1
			    || ch < 0 || ch > 4) {
				fprintf(stderr, "%s: mode: invalid channel "
					"\"%s\"\n", prgname,  argv[1]);
				return -1;
			}
			if (strlen(argv[2]) != 1) {
				fprintf(stderr, "%s: mode: invalid mode "
					"\"%s\"\n", prgname,  argv[2]);
				return -1;
			}
			if (one_mode(argv[2][0], ch) < 0)
				return -1;
			argv += 2;
			argc -= 2;
		}
	}
	ifr.ifr_data = (void *)cmd;
	if (ioctl(sock, PRIV_MEZZANINE_CMD, &ifr) < 0) {
		fprintf(stderr, "%s: ioctl(PRIV_MEZZANINE_CMD(%s)): %s\n",
			prgname, ifname, strerror(errno));
			return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{

	prgname = argv[0];
	argv++, argc--;

	if (argc < 2) {
		fprintf(stderr, "%s: use \"%s <netdev> <cmd> [...]\"\n",
			prgname, prgname);
		exit(1);
	}
	ifname = argv[0];
	argv++, argc--;

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

	/*
	 * Parse the command line:
	 *
	 * pulse <ch> .<len> <seconds>.<fraction>
	 * pulse <ch> .<len> now
	 * pulse <ch> .<len> +<seconds>.<fraction>
	 *
	 * stamp [<channel>]
	 * stampm [<mask>]
	 *
	 * mode <01234>
	 * mode <ch> <mode> [...]
	 */
	if (!strcmp(argv[0], "pulse")) {
		cmd->command = WR_DIO_CMD_PULSE;
		if (scan_pulse(argc, argv) < 0)
			exit(1);
	} else if (!strcmp(argv[0], "stamp")) {
		cmd->command = WR_DIO_CMD_STAMP;
		if (scan_stamp(argc, argv, 0 /* no mask */) < 0)
			exit(1);
	} else if (!strcmp(argv[0], "stampm")) {
		cmd->command = WR_DIO_CMD_STAMP;
		if (scan_stamp(argc, argv, 1 /* mask */) < 0)
			exit(1);
	} else if (!strcmp(argv[0], "mode")) {
		cmd->command = WR_DIO_CMD_INOUT;
		if (scan_inout(argc, argv) < 0)
			exit(1);
	} else {
		fprintf(stderr, "%s: unknown command \"%s\"\n", prgname,
			argv[0]);
		exit(1);
	}

	ifr.ifr_data = (void *)cmd;
	exit(0);
}
