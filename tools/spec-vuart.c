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

#include "spec-tools.h"
#include "wb_uart.h"

void *vuart_regs;

int vc_rx(void *ptr)
{
	int csr ;
	csr = *(int *)(ptr + UART_REG_HOST_RDR);
	if(csr & UART_HOST_RDR_RDY)
		return UART_HOST_RDR_DATA_R(csr);
	else
		return -1;
}

void vc_tx(void *ptr, int c)
{
	*(int *)(ptr + UART_REG_HOST_TDR) = UART_HOST_TDR_DATA_W(c);
}

int transfer_byte(int from, int is_control, void *ptr) {
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
		vc_tx(ptr, c);
	} else {
		fprintf(stderr, "\nnothing to read. probably port disconnected.\n");
		return -2;
	}
	return 0;
}


void term_main(int keep_term)
{
	struct termios oldkey, newkey;       //place tor old and new port settings for keyboard teletype
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
		int ret, rx;
		struct timeval tv = {0, 10000};
		
		FD_ZERO(&fds);
		FD_SET(STDIN_FILENO, &fds);

		ret = select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
		if(ret == -1) {
			perror("select");
		} else if (ret > 0) {
			if(FD_ISSET(STDIN_FILENO, &fds)) {
				need_exit = transfer_byte(STDIN_FILENO, 1, vuart_regs);
			}
		}

		while((rx = vc_rx(vuart_regs)) >= 0)
			fprintf(stderr,"%c", (char)rx);

	}

	if(!keep_term)
		tcsetattr(STDIN_FILENO,TCSANOW,&oldkey);
}

int main(int argc, char **argv)
{
	int bus = -1, dev_fn = -1, c;
	uint32_t vuart_base = 0xe0500;
	void *map_base;
	int keep_term = 0;

	while ((c = getopt (argc, argv, "b:d:u:k")) != -1)
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
		default:
			fprintf(stderr,
				"Use: \"%s [-b bus] [-d devfn] [-u VUART base] [-k]\"\n", argv[0]);
			fprintf(stderr,
				"By default, the first available SPEC is used and the VUART is assumed at 0x%x.\n \
-k option keeps the terminal config unchanged.", vuart_base);
			exit(1);
		}
	}

	map_base = spec_map_area(bus, dev_fn, BASE_BAR0, 0x100000);
	if(!map_base)
	{
		fprintf(stderr, "%s: can't map the SPEC @ %02x:%02x\n", argv[0], bus, dev_fn);
		exit(1);
	}

	vuart_regs = map_base + vuart_base;
	term_main(keep_term);
	return 0;
}
