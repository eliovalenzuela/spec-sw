/*
 * Trivial library function to return one of the spec memory addresses
 */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <sys/signal.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include "spec-tools.h"


/* Checks if there's a SPEC card at *bus/*def_fn. If one (or both) parameters are < 0, takes first available card and returns 0.
   If no cards have been detected, returns -1 */
int spec_scan(int *bus, int *devfn)
{
	struct dirent **namelist;
	int n, found = 0;
	int my_bus, my_devfn;

	n = scandir("/sys/bus/pci/drivers/spec", &namelist, 0, 0);
	if (n < 0)
	{
		perror("scandir");
		exit(-1);
	} else {
		while (n--) 
		{
			if(!found && sscanf(namelist[n]->d_name, "0000:%02x:%02x.0", &my_bus, &my_devfn) == 2)
			{
				if(*bus < 0) *bus = my_bus;
				if(*devfn < 0) *devfn = my_devfn;
				if(*bus == my_bus && *devfn == my_devfn)
					found = 1;
			}
			free(namelist[n]);
		}
		free(namelist);
	}
        
	if(!found)
	{
		fprintf(stderr,"Can't detect any SPEC card :(\n");
		return -1;
	}
        
	return 0;
	
}

/* Maps a particular BAR of given SPEC card and returns its virtual address 
   (or NULL in case of failure) */

void *spec_map_area(int bus, int dev, int bar, size_t size)
{
	char path[1024];
	int fd;
	void *ptr;

	if(spec_scan(&bus, &dev) < 0)
		return NULL;

	snprintf(path, sizeof(path), "/sys/bus/pci/drivers/spec/0000:%02x:%02x.0/resource%d", bus, dev, bar);
    
	fd = open(path, O_RDWR | O_SYNC);
	if(fd <= 0)
		return NULL;

	ptr = mmap(NULL, size & ~(getpagesize()-1), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    
	if((int)ptr == -1)
	{
		close(fd);
		return NULL;
	}
    
	return ptr;
}
