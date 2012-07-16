
LINUX ?= /lib/modules/$(shell uname -r)/build

ccflags-y = -I$M/include

obj-m = fmc-core.o
obj-m += spec.o
obj-m += wr-nic.o

spec-objs = spec-core.o loader-ll.o

all modules:
	$(MAKE) -C $(LINUX) M=$(shell /bin/pwd) modules

install modules_install:
	$(MAKE) -C $(LINUX) M=$(shell /bin/pwd) modules_install


clean:
	rm -rf *.o *~  .*.cmd *.ko *.mod.c .tmp_versions Module.symvers \
		Module.markers modules.order
