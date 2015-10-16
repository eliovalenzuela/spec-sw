LINUX ?= /lib/modules/$(shell uname -r)/build

# by default use the fmc-bus within the repository
FMC_BUS ?= $(shell pwd)/../fmc-bus/
FMC_DRV ?= $(FMC_BUS)/kernel/

GIT_VERSION = $(shell cd $(src); git describe --dirty --long --tags)

all modules:
	$(MAKE) -C $(LINUX) M=$(shell /bin/pwd) modules

install modules_install:
	$(MAKE) -C $(LINUX) M=$(shell /bin/pwd) modules_install

# be able to run the "clean" rule even if $(LINUX) is not valid
clean:
	rm -rf *.o *~  .*.cmd *.ko *.mod.c .tmp_versions Module.symvers \
		Module.markers modules.order
	rm -rf wr_nic/*.o wr_nic/*~
