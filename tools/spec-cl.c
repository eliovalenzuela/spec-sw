/*
 * A tool to program our soft-core (LM32) within the SPEC.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <getopt.h>

#include "speclib.h"

static char git_version[] = "version: " GIT_VERSION;

static void print_version(char *pname)
{
	printf("%s %s\n", pname, git_version);
	printf("%s\n", libspec_version_s);
}

int main(int argc, char **argv)
{
	int bus = -1, dev_fn = -1, c;
	uint32_t lm32_base = 0x80000;
	void *card;


	while ((c = getopt (argc, argv, "b:d:c:V")) != -1)
	{
		switch(c)
		{
		case 'b':
			sscanf(optarg, "%i", &bus);
			break;
		case 'd':
			sscanf(optarg, "%i", &dev_fn);
			break;
		case 'c':
			sscanf(optarg, "%i", &lm32_base);
			break;
		case 'V':
			print_version(argv[0]);
			exit(0);
		default:
			fprintf(stderr,
				"Use: \"%s [-V] [-b bus] [-d devfn] "
				"[-c lm32 base address] <lm32_program.bin>\"\n",
				argv[0]);
			fprintf(stderr,
				"By default, the first available SPEC is used "
				"and the LM32 is assumed at 0x%x.\n",
				lm32_base);
			exit(1);
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "%s: Expected binary name after options.\n",
			argv[0]);
		exit(1);
	}

	card = spec_open(bus, dev_fn);
	if(!card)
	{
		fprintf(stderr, "%s: Can't detect a SPEC card under the given "
			"adress.\nMake sure a SPEC card is present in your PC, "
			"the driver is loaded and you run the program as root.\n", argv[0]);
		exit(1);
	}

	if(spec_load_lm32(card, argv[optind], lm32_base) < 0)
	{
		fprintf(stderr, "%s: Loader failure.\n", argv[0]);
		exit(1);
	}

	spec_close(card);

	exit (0);
}
