
LINUX ?= /lib/modules/$(shell uname -r)/build

FMC_DRV ?= $(shell ../check-fmc-bus)
export FMC_DRV

KBUILD_EXTRA_SYMBOLS := $(FMC_DRV)/Module.symvers

GIT_VERSION = $(shell cd $(src); git describe --dirty --long --tags)

ccflags-y += -I$M/include -I$(FMC_DRV)/include
ccflags-y += $(WR_NIC_CFLAGS)
ccflags-y += -DGIT_VERSION=\"$(GIT_VERSION)\"


# this is a bad hack. Sometimes we are a submodule, and wr-nic can
# only compile with recent versions, so let the caller disable it
# FIXME: this is incorrect if we get copied to the kernel proper.
CONFIG_WR_NIC ?= m

obj-m += spec.o
obj-$(CONFIG_WR_NIC) += wr-nic.o

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
