/*
 * A tool to read SPEC-internal memory (only BAR0)
 *
 * Alessandro Rubini and Tomasz Wlostowski 2012 for CERN, GPLv2 or later.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "speclib.h"

void help(char *name)
{
	fprintf(stderr,
		"Use: \"%s [-b bus] [-d devfn] [-g] <offset> [<value>]\"\n",
		name);
	fprintf(stderr, "By default, the first available SPEC is used.\n");
	exit(1);
}

int main(int argc, char **argv)
{
	int i, bar = BASE_BAR0;
	int bus = -1, dev_fn = -1, c;
	uint32_t *ptr;
	uint32_t uarg[2];
	int do_write;
	void *card;
	void *map_base;
	char *end;

	while ((c = getopt (argc, argv, "b:d:g")) != -1)
	{
		switch(c)
		{
		case 'b':
			sscanf(optarg, "%i", &bus);
			break;
		case 'd':
			sscanf(optarg, "%i", &dev_fn);
			break;
		case 'g':
			bar = BASE_BAR4;
			break;
		default:
			help(argv[0]);
		}
	}
	if (optind  >= argc || optind < argc - 2)
		help(argv[0]);
	do_write = (optind == argc - 2);

	/* convert the trailing hex number or numbers */
	for (i = 0; i <= do_write; i++) {
		uarg[i] = strtol(argv[optind + i], &end, 16);
		if (end && *end) {
			fprintf(stderr, "%s: \"%s\" is not an hex number\n",
				argv[0], argv[optind + i]);
			exit(1);
		}
	}
	if (uarg[0] & 3) {
		fprintf(stderr, "%s: address \"%s\" not multiple of 4\n",
			argv[0], argv[optind + 0]);
		exit(1);
	}

	card = spec_open(bus, dev_fn);
	if (!card) {
		fprintf(stderr, "%s: No SPEC card at bus %i, devfn %i\n",
			argv[0], bus, dev_fn);
		fprintf(stderr, "  please make sure the address is correct,\n"
			"  spec.ko is loaded and you run as superuser.\n");
		exit(1);
	}

	map_base = spec_get_base(card, bar);
	if(!map_base || map_base == (void *) -1) {
		fprintf(stderr, "%s: mmap(/dev/mem): %s\n", argv[0],
			strerror(errno));
		exit(1);
	}

	ptr = map_base + uarg[0];
	/* by default, operate quietly (only report read value) */
	if (!do_write) {
		uarg[1] = *ptr;
		if (!getenv("VERBOSE"))
			printf("%08x\n", uarg[1]);
	} else {
		*ptr = uarg[1];
	}

	/* be verbose, if so requested */
	if (getenv("VERBOSE")) {
		if (argc == 2)
			printf("%08x == %08x\n", uarg[0], uarg[1]);
		else
			printf("%08x := %08x\n", uarg[0], uarg[1]);
	}
	spec_close(card);

	exit (0);
}
