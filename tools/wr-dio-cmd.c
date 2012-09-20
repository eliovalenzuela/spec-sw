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

static int scan_pulse(int argc, char **argv)
{
	unsigned long frac_ns;
	char *s;
	int i, n;

	if (argc != 4) {
		fprintf(stderr, "%s: %s: wrong number of arguments\n",
			prgname, argv[0]);
		return -1;
	}
	if (sscanf(argv[1], "%hi%c", &cmd->channel, &c) != 1
		|| cmd->channel < 0
		|| cmd->channel > 4) {
		fprintf(stderr, "%s: %s: not a channel number \"%s\"\n",
			prgname, argv[0], argv[1]);
		return -1;
	}

	/*
 	 * Hairy: if we scan "%ld%lf", the 0.009999 will become 9998 micro.
	 * Thus, scan as integer and string, so we can count leading zeros
	 */
	n = strlen(argv[2]) - 1;
	if (n > 9) {
		n = 9;
		argv[2][10] = '\0';
	}
	if (sscanf(argv[2], ".%ld%c", &frac_ns, &c) != 1) {
		fprintf(stderr, "%s: %s: not a fraction \"%s\" (please use "
			"leading dot)\n", prgname, argv[0], argv[2]);
		return -1;
	}
	while (n < 9) {
		frac_ns *= 10;
		n++;
	}

	cmd->t[1].tv_nsec = frac_ns;

	/* Same problem with the time. But now it's integer only (FIXME) */
	frac_ns = 0;
	if (!strcmp(argv[3], "now"))
		cmd->flags |= WR_DIO_F_NOW;
	i = sscanf(argv[3], "%ld.%ld%c", &cmd->t[0].tv_sec, &frac_ns, &c);
	if (i != 1 && i != 2 && !(cmd->flags & WR_DIO_F_NOW)) {
		fprintf(stderr, "%s: %s: not a time value \"%s\"\n",
		       prgname, argv[0], argv[3]);
		return -1;
	}
	if (i == 2) {
		/* FIXME: factorize this scanning of fractions */
		s = strchr(argv[3], '.') + 1;
		n = strlen(s);
		if (n > 9) {
			n = 9;
			s[9] = '\0';
		}
		while (n < 9) {
			frac_ns *= 10;
			n++;
		}
	}
	cmd->t[0].tv_nsec = frac_ns;

	if (argv[3][0] == '+')
		cmd->flags |= WR_DIO_F_REL;

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

	if (argc == 1) {
		ismask = 1;
		ch = 0x1f;
	} else if (argc == 2) {
		if (sscanf(argv[1], "%i%c", &ch, &c) != 1) {
			fprintf(stderr, "%s: %s: not a number \"%s\"\n",
				prgname, argv[0], argv[1]);
			exit(1);
		}
		if (ch < 0 || ch > 31 || (!ismask && ch > 4)) {
			fprintf(stderr, "%s: %s: out of range value \"%s\"\n",
				prgname, argv[0], argv[1]);
			exit(1);
		}
	} else {
		fprintf(stderr, "%s: %s: wrong number of arguments\n",
			prgname, argv[0]);
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
	 * TODO: stamp <channel>
	 * TODO: stampm <mask>
	 **/
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
	} else {
		fprintf(stderr, "%s: unknown command \"%s\"\n", prgname,
			argv[0]);
		exit(1);
	}

	ifr.ifr_data = (void *)cmd;
	exit(0);
}
