/* A simple console for accessing the SPEC virtual UART (i.e. for communicating with the WR Core shell
   from a Linux terminal. */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <sys/signal.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <getopt.h>
#include <errno.h>

#include "speclib.h"

static char git_version[] = "version: " GIT_VERSION;

static void print_version(char *pname)
{
	printf("%s %s\n", pname, git_version);
	printf("%s\n", libspec_version_s);
}

static void *card;

static int transfer_byte(int from, int is_control) {
	char c;
	int ret;
	do {
		ret = read(from, &c, 1);
	} while (ret < 0 && errno == EINTR);
	if(ret == 1) {
		if(is_control) {
			if(c == '\x01') { // C-a
				return -1;
			}
		}
		spec_vuart_tx(card, &c, 1);
	} else {
		fprintf(stderr, "nothing to read. Port disconnected?\n");
		return -2;
	}
	return 0;
}


void term_main(int keep_term)
{
	struct termios oldkey, newkey;
	//above is place for old and new port settings for keyboard teletype
	int need_exit = 0;

	fprintf(stderr, "[press C-a to exit]\n");

	if(!keep_term) {
		tcgetattr(STDIN_FILENO,&oldkey);
		newkey.c_cflag = B9600 | CS8 | CLOCAL | CREAD;
		newkey.c_iflag = IGNPAR;
		newkey.c_oflag = 0;
		newkey.c_lflag = 0;
		newkey.c_cc[VMIN]=1;
		newkey.c_cc[VTIME]=0;
		tcflush(STDIN_FILENO, TCIFLUSH);
		tcsetattr(STDIN_FILENO,TCSANOW,&newkey);
	}
	while(!need_exit) {
		fd_set fds;
		int ret;
		char rx;
		struct timeval tv = {0, 10000};

		FD_ZERO(&fds);
		FD_SET(STDIN_FILENO, &fds);

		ret = select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
		if(ret == -1) {
			perror("select");
		} else if (ret > 0) {
			if(FD_ISSET(STDIN_FILENO, &fds)) {
				need_exit = transfer_byte(STDIN_FILENO, 1);
			}
		}

		while((spec_vuart_rx(card, &rx, 1)) == 1)
			fprintf(stderr,"%c", rx);

	}

	if(!keep_term)
		tcsetattr(STDIN_FILENO,TCSANOW,&oldkey);
}

#define __WR_VUART_OFFSET 0x20500 /* typical location */

int main(int argc, char **argv)
{
	int bus = -1, dev_fn = -1, c;
	uint32_t vuart_base = ~0;
	int keep_term = 0;

	while ((c = getopt (argc, argv, "b:d:u:kV")) != -1)
	{
		switch(c)
		{
		case 'b':
			sscanf(optarg, "%i", &bus);
			break;
		case 'd':
			sscanf(optarg, "%i", &dev_fn);
			break;
		case 'u':
			sscanf(optarg, "%i", &vuart_base);
			break;
		case 'k':
			keep_term = 1;
			break;
		case 'V':
			print_version(argv[0]);
			exit(0);
		default:
			fprintf(stderr,
				"Use: \"%s [-V] [-b bus] [-d devfn] "
				"[-u VUART base] [-k]\"\n", argv[0]);
			fprintf(stderr,
				"By default, the first available SPEC is used\n");
			fprintf(stderr,
				"-k option keeps the prev terminal config\n");
			fprintf(stderr,
				"-u to set the VUART offset within "
				"the bitstream. Typically at 0x%x\n",
				__WR_VUART_OFFSET);
			exit(1);
		}
	}

	if (vuart_base == ~0) {
		fprintf(stderr, "\"-u\" parameter is mandatory\n");
		exit(1);
	}

	card = spec_open(bus, dev_fn);
	if(!card)
	{
		fprintf(stderr, "Can't detect a SPEC card under the given "
			"adress.\nMake sure a SPEC card is present in your PC, "
			"the driver is loaded and you run the program as root.\n");
		exit(1);
	}

	spec_vuart_init(card, vuart_base);
	term_main(keep_term);
	spec_close(card);

	return 0;
}
