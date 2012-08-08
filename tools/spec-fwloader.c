/*
 * A tool to program the FPGA within the SPEC.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <getopt.h>

#include "speclib.h"

int main(int argc, char **argv)
{
	int bus = -1, dev_fn = -1, c;
	void *card;

	while ((c = getopt (argc, argv, "b:d:")) != -1)
	{
		switch(c)
		{
		case 'b':
			sscanf(optarg, "%i", &bus);
			break;
		case 'd':
			sscanf(optarg, "%i", &dev_fn);
			break;
		default:
			fprintf(stderr,
				"Use: \"%s [-b bus] [-d devfn] "
				"<fpga_bitstream.bin>\"\n", argv[0]);
			fprintf(stderr, "By default, the first available SPEC "
				"is used.\n");
			exit(1);
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "Expected binary name after options.\n");
		exit(1);
	}

	card = spec_open(bus, dev_fn);
	if(!card)
	{
		fprintf(stderr, "Can't detect a SPEC card under the given "
			"adress. Make sure a SPEC card is present in your PC "
			"and the driver is loaded.\n");
		exit(1);
	}

	if(spec_load_bitstream(card, argv[optind]) < 0)
	{
		fprintf(stderr, "Loader failure.\n");
		exit(1);
	}

	spec_close(card);

	exit (0);
}
