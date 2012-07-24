
LINUX ?= /lib/modules/$(shell uname -r)/build

ccflags-y = -I$M/include

obj-m = fmc-core.o
obj-m += spec.o
obj-m += fmc-trivial.o
obj-m += fmc-write-eeprom.o
obj-m += wr-nic.o

spec-y = spec-pci.o
spec-y += spec-fmc.o
spec-y += spec-i2c.o
spec-y += loader-ll.o
spec-y += spec-gpio-no.o
spec-$(CONFIG_GPIOLIB) += spec-gpio.o

all modules:
	$(MAKE) -C $(LINUX) M=$(shell /bin/pwd) modules

install modules_install:
	$(MAKE) -C $(LINUX) M=$(shell /bin/pwd) modules_install


clean:
	rm -rf *.o *~  .*.cmd *.ko *.mod.c .tmp_versions Module.symvers \
		Module.markers modules.order
