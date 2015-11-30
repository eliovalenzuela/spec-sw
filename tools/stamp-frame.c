/*
 * Copyright (C) 2010,2012 CERN (www.cern.ch)
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
#include <netpacket/packet.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <arpa/inet.h>

#include "net_tstamp.h" /* Actually, <linux/net_tstamp.h> */

#ifndef SO_TIMESTAMPNS
# define SO_TIMESTAMPNS 35
# define SCM_TIMESTAMPNS            SO_TIMESTAMPNS
#endif

#ifndef SO_TIMESTAMPING
# define SO_TIMESTAMPING         37
# define SCM_TIMESTAMPING        SO_TIMESTAMPING
#endif

#ifndef SIOCSHWTSTAMP
# define SIOCSHWTSTAMP 0x89b0
#endif

#ifndef ETH_P_1588
# define ETH_P_1588   0x88F7
#endif

static char git_version[] = "version: " GIT_VERSION;

/* This structure is used to collect stamping information */
struct ts_data {
	struct timespec ns;
	struct timespec hw[3]; /* software, hw-sys, hw-raw */
	int error;
};

/* We can print such stamp info. Returns -1 with errno set on error */
int print_stamp(FILE *out, char *prefix, struct ts_data *tstamp, FILE *err)
{
	int i;
	static char *names[] = {"sw    ", "hw-sys", "hw-raw"};

	if (tstamp->error) {
		if (err)
			fprintf(err, "%s: %s\n", prefix, strerror(errno));
		errno = tstamp->error;
		return -1;
	}
	fprintf(out, "%s     ns: %10li.%09li\n", prefix, tstamp->ns.tv_sec,
		tstamp->ns.tv_nsec);
	for (i = 0; i < 3; i++)
		fprintf(out, "%s %s: %10li.%09li\n", prefix, names[i],
			tstamp->hw[i].tv_sec,
			tstamp->hw[i].tv_nsec);
	fprintf(out, "\n");
	return 0;
}


/*
 * This function opens a socket and configures it for stamping.
 * It is a library function, in a way, and was used as such
 * when part of the "testing" programs of wr-switch-sw
 */
int make_stamping_socket(FILE *errchan, char *argv0, char *ifname,
			 int tx_type, int rx_filter, int bits,
			 unsigned char *macaddr)
{
	struct ifreq ifr;
	struct sockaddr_ll addr;
	struct hwtstamp_config hwconfig;
	int sock, iindex, enable = 1;

	sock = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (sock < 0 && errchan)
		fprintf(errchan, "%s: socket(): %s\n", argv0,
			strerror(errno));
	if (sock < 0)
		return sock;

	memset (&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, ifname);

	/* hw interface information */
	if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
		if (errchan)
			fprintf(errchan, "%s: SIOCGIFINDEX(%s): %s\n", argv0,
				ifname, strerror(errno));
		close(sock);
		return -1;
	}

	iindex = ifr.ifr_ifindex;
	if (ioctl(sock, SIOCGIFHWADDR, &ifr) < 0) {
		if (errchan)
			fprintf(errchan, "%s: SIOCGIFHWADDR(%s): %s\n", argv0,
				ifname, strerror(errno));
		close(sock);
		return -1;
	}
	memcpy(macaddr, ifr.ifr_hwaddr.sa_data, 6);

	/* Also, enable stamping for the hw interface */
	memset(&hwconfig, 0, sizeof(hwconfig));
	hwconfig.tx_type = tx_type;
	hwconfig.rx_filter = rx_filter;
	ifr.ifr_data = (void *)&hwconfig;
	if (ioctl(sock, SIOCSHWTSTAMP, &ifr) < 0) {
		if (errchan)
			fprintf(errchan, "%s: SIOCSHWSTAMP(%s): %s\n", argv0,
				ifname, strerror(errno));
		close(sock);
		return -1;
	}

	/* bind and setsockopt */
	memset(&addr, 0, sizeof(addr));
	addr.sll_family = AF_PACKET;
	addr.sll_protocol = htons(ETH_P_ALL);
	addr.sll_ifindex = iindex;
	addr.sll_pkttype = PACKET_OUTGOING;
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		if (errchan)
			fprintf(errchan, "%s: SIOCSHWSTAMP(%s): %s\n", argv0,
				ifname, strerror(errno));
		close(sock);
		return -1;
	}
	if (setsockopt(sock, SOL_SOCKET, SO_TIMESTAMP,
			       &enable, sizeof(enable)) < 0) {
		if (errchan)
			fprintf(errchan, "%s: setsockopt(TIMESTAMP): %s\n",
				argv0, strerror(errno));
		close(sock);
		return -1;
	}
	if (setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING,
			       &bits, sizeof(bits)) < 0) {
		if (errchan)
			fprintf(errchan, "%s: setsockopt(TIMESTAMPING): %s\n",
				argv0, strerror(errno));
		close(sock);
		return -1;
	}
	return sock;
}

/*
 * Another "library" function, actually only used by the following two
 */
static void __collect_data(struct msghdr *msgp, struct ts_data *tstamp)
{
	struct cmsghdr *cm;
	struct timespec *tsptr;

	if (!tstamp)
		return;
	memset(tstamp, 0, sizeof(*tstamp));

	/* Extract data from the cmsg */
	for (cm = CMSG_FIRSTHDR(msgp); cm; cm = CMSG_NXTHDR(msgp, cm)) {
		tsptr = (struct timespec *)CMSG_DATA(cm);
		if (0) {
			printf("level %i, type %i, len %zi\n", cm->cmsg_level,
			       cm->cmsg_type, cm->cmsg_len);
		}
		if (cm->cmsg_level != SOL_SOCKET)
			continue;
		if (cm->cmsg_type == SO_TIMESTAMPNS)
			tstamp->ns = *tsptr;
		if (cm->cmsg_type == SO_TIMESTAMPING)
			memcpy(tstamp->hw, tsptr, sizeof(tstamp->hw));
	}
}

/*
 * These functions are like send/recv but handle stamping too.
 */
ssize_t send_and_stamp(int sock, void *buf, size_t len, int flags,
	struct ts_data *tstamp)
{
	struct msghdr msg; /* this line and more from timestamping.c */
	struct iovec entry;
	struct sockaddr_ll from_addr;
	struct {
		struct cmsghdr cm;
		char control[512];
	} control;
	char data[3*1024];
	int i, j, ret;

	ret = send(sock, buf, len, flags);
	if (ret < 0)
		return ret;

	/* Then, get back from the error queue */
	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = &entry;
	msg.msg_iovlen = 1;
	entry.iov_base = data;
	entry.iov_len = sizeof(data);
	msg.msg_name = (caddr_t)&from_addr;
	msg.msg_namelen = sizeof(from_addr);
	msg.msg_control = &control;
	msg.msg_controllen = sizeof(control);

	j = 100; /* number of trials */
	while ( (i = recvmsg(sock, &msg, MSG_ERRQUEUE)) < 0 && j--)
		usleep(10000); /* retry for 1 second */
	if (i < 0) {
		if (tstamp) {
			memset(tstamp, 0, sizeof(*tstamp));
			tstamp->error = ETIMEDOUT;
		}
		return ret;
	}
	if (getenv("STAMP_VERBOSE")) {
		int b;
		printf("send %i =", i);
		for (b = 0; b < i && b < 20; b++)
			printf(" %02x", data[b] & 0xff);
		putchar('\n');
	}

	/* FIX<E: Check that the actual data is what we sent */

	__collect_data(&msg, tstamp);

	return ret;
}

ssize_t recv_and_stamp(int sock, void *buf, size_t len, int flags,
	struct ts_data *tstamp)
{
	int ret;
	struct msghdr msg;
	struct iovec entry;
	struct sockaddr_ll from_addr;
	struct {
		struct cmsghdr cm;
		char control[512];
	} control;

	if (0) {
		/* we can't really call recv, do it with cmsg alone */
		ret = recv(sock, buf, len, flags);
	} else {
		memset(&msg, 0, sizeof(msg));
		msg.msg_iov = &entry;
		msg.msg_iovlen = 1;
		entry.iov_base = buf;
		entry.iov_len = len;
		msg.msg_name = (caddr_t)&from_addr;
		msg.msg_namelen = sizeof(from_addr);
		msg.msg_control = &control;
		msg.msg_controllen = sizeof(control);

		ret = recvmsg(sock, &msg, 0);
		if (ret < 0)
			return ret;

		if (getenv("STAMP_VERBOSE")) {
			int b;
			printf("recv %i =", ret);
			for (b = 0; b < ret && b < 20; b++)
				printf(" %02x", ((char *)buf)[b] & 0xff);
			putchar('\n');
		}
		__collect_data(&msg, tstamp);
	}
	return ret;
}

/* Add and subtract timespec */
void ts_add(struct timespec *t1, struct timespec *t2)
{
	t1->tv_sec += t2->tv_sec;
	t1->tv_nsec += t2->tv_nsec;
	if (t1->tv_nsec > 1000 * 1000 * 1000) {
		t1->tv_nsec -= 1000 * 1000 * 1000;
		t1->tv_sec++;
	}
}

void ts_sub(struct timespec *t1, struct timespec *t2)
{
	t1->tv_sec -= t2->tv_sec;
	t1->tv_nsec -= t2->tv_nsec;
	if (t1->tv_nsec < 0) {
		t1->tv_nsec += 1000 * 1000 * 1000;
		t1->tv_sec--;
	}
}


/*
 * Ok, the library-like part is over, we are at main now
 */
#define STAMP_PROTO 0xf001

/* This is the frame we are exchanging back and forth */
struct frame {
	struct ether_header h;
	uint16_t phase; /* 0 = transmit, 1 = tx follow up */
	struct timespec ts[4];
	unsigned char filler[64]; /* to reach minimum size for sure */
};

void report_times(struct timespec *ts)
{
	struct timespec rtt, tmp, fw, bw;
	int i;

	for (i = 0; i < 4; i++)
		printf("timestamp    T%i: %9li.%09li\n", i+1,
		       ts[i].tv_sec, ts[i].tv_nsec);

	/* calculate round trip time, forward, backward */
	rtt = ts[3];
	ts_sub(&rtt, &ts[0]);
	tmp = ts[2];
	ts_sub(&tmp, &ts[1]);
	ts_sub(&rtt, &tmp);

	fw = ts[1];
	ts_sub(&fw, &ts[0]);
	bw = ts[3];
	ts_sub(&bw, &ts[2]);

	printf("round trip time: %9li.%09li\n", rtt.tv_sec, rtt.tv_nsec);
	printf("forward    time: %9li.%09li\n", fw.tv_sec, fw.tv_nsec);
	printf("backward   time: %9li.%09li\n", bw.tv_sec, bw.tv_nsec);
}

/* send a frame, and then again after filling the tx time at offset given */
void send_one_with_followup(int sock, struct frame *f, unsigned char *mac,
			    int offset)
{
	struct ts_data stamp;

	/* Fill ether header */
	memset(&f->h.ether_dhost, 0xff, ETH_ALEN); /* broadcast */
	memcpy(&f->h.ether_shost, mac, ETH_ALEN);
	f->h.ether_type = htons(STAMP_PROTO);

	f->phase = 0;
	if (send_and_stamp(sock, f, sizeof(*f), 0, &stamp) < 0)
		fprintf(stderr, "send_and_stamp: %s\n", strerror(errno));
	f->phase = 1;
	f->ts[offset] = stamp.hw[2]; /* hw raw */
	if (send_and_stamp(sock, f, sizeof(*f), 0, NULL) < 0)
		fprintf(stderr, "send_and_stamp: %s\n", strerror(errno));

	if (getenv("STAMP_PRINT_ALL"))
		print_stamp(stdout, "send", &stamp, stderr);
}

/* receive a frame, timestamping it, and receive the followup too; save ts */
void recv_one_with_followup(int sock, struct frame *f, unsigned char *mac,
			    int offset)
{
	struct ts_data stamp;
	int ret;

	while (1) { /* repeat until a good frame is received */
		ret = recv_and_stamp(sock, f, sizeof(*f), MSG_TRUNC, &stamp);
		if (ret < 0) {
			fprintf(stderr, "recv_and_stamp: %s\n",
				strerror(errno));
			continue;
		}
		if (ret != sizeof(*f))
			continue;
		if (ntohs(f->h.ether_type) != STAMP_PROTO)
			continue;
		if (f->phase != 0)
			continue;
		break;
	}
	/* receive another one, lazily don't wait */
	if (recv_and_stamp(sock, f, sizeof(*f), MSG_TRUNC, NULL) < 0)
		fprintf(stderr, "recv_and_stamp: %s\n",
			strerror(errno));
	f->ts[offset] = stamp.hw[2];

	if (getenv("STAMP_PRINT_ALL"))
		print_stamp(stdout, "recv", &stamp, stderr);

}

static void print_version(char *pname)
{
	printf("%s %s\n", pname, git_version);
}

int main(int argc, char **argv)
{
	static struct frame f;
	int sock;
	unsigned char macaddr[6];
	int listenmode = 0;
	int howto = SOF_TIMESTAMPING_MASK; /* everything */

	if ((argc == 2) && (!strcmp(argv[1], "-V"))) {
		print_version(argv[0]);
		exit(0);
	}
	
	/* From ./net_tstamp.h, these are the "howto" values
	 *
	 * SOF_TIMESTAMPING_TX_HARDWARE = 1,
	 * SOF_TIMESTAMPING_TX_SOFTWARE = 2
	 * SOF_TIMESTAMPING_RX_HARDWARE = 4,
	 * SOF_TIMESTAMPING_RX_SOFTWARE = 8,
	 * SOF_TIMESTAMPING_SOFTWARE =   16,
	 * SOF_TIMESTAMPING_SYS_HARDWARE = 32,
	 * SOF_TIMESTAMPING_RAW_HARDWARE = 64,
	 */

	if (argc == 3 && !strcmp(argv[2], "listen")) {
		listenmode = 1;
		argc--;
	}
	if (argc != 2) {
		fprintf(stderr, "%s: Use \"%s [-V] <ifname> [listen]\n", argv[0],
			argv[0]);
		exit(1);
	}

	printf("%s: Using interface %s, with all timestamp options active\n",
	       argv[0], argv[1]);

	/* Create a socket to use for stamping, use the library code above */
	sock = make_stamping_socket(stderr, argv[0], argv[1],
				    HWTSTAMP_TX_ON, HWTSTAMP_FILTER_ALL,
				    howto, macaddr);
	if (sock < 0) /* message already printed */
		exit(1);

	if (listenmode) {
		/* forever reply */
		while (1) {
			recv_one_with_followup(sock, &f, macaddr, 1);
			send_one_with_followup(sock, &f, macaddr, 2);
		}
	}

	/* send mode:  send first, then receive, then print */
	send_one_with_followup(sock, &f, macaddr, 0);
	recv_one_with_followup(sock, &f, macaddr, 3);

	report_times(f.ts);

	exit(0);
}

