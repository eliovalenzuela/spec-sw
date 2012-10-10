
LINUX ?= /lib/modules/$(shell uname -r)/build

ccflags-y += -I$M/include
ccflags-y += $(WR_NIC_CFLAGS)

obj-m = fmc.o
obj-m += spec.o
obj-m += fmc-trivial.o
obj-m += fmc-write-eeprom.o
obj-m += wr-nic.o

fmc-y = fmc-core.o
fmc-y += fmc-sdb.o

spec-y = spec-pci.o
spec-y += spec-fmc.o
spec-y += spec-i2c.o
spec-y += loader-ll.o
spec-y += spec-gpio-no.o
spec-$(CONFIG_GPIOLIB) += spec-gpio.o

wr-nic-y = wr-nic-core.o
wr-nic-y += wr-nic-eth.o
wr-nic-y += wr-nic-dio.o
wr-nic-y += wr_nic/device.o
wr-nic-y += wr_nic/endpoint.o
wr-nic-y += wr_nic/ethtool.o
wr-nic-y += wr_nic/nic-core.o
wr-nic-y += wr_nic/timestamp.o
wr-nic-y += wr_nic/pps.o
wr-nic-$(CONFIG_GPIOLIB) += wr-nic-gpio.o


all modules:
	$(MAKE) -C $(LINUX) M=$(shell /bin/pwd) modules

install modules_install:
	$(MAKE) -C $(LINUX) M=$(shell /bin/pwd) modules_install

# be able to run the "clean" rule even if $(LINUX) is not valid
clean:
	rm -rf *.o *~  .*.cmd *.ko *.mod.c .tmp_versions Module.symvers \
		Module.markers modules.order
