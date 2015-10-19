# include parent_common.mk for buildsystem's defines
#use absolute path for REPO_PARENT
REPO_PARENT=$(shell /bin/pwd)/../..
-include $(REPO_PARENT)/parent_common.mk

LINUX ?= /lib/modules/$(shell uname -r)/build

# by default use the fmc-bus within the repository
FMC_BUS ?= $(shell pwd)/../fmc-bus/
# FMC_BUS_ABS has to be absolut path, due to beeing passed to the Kbuild
FMC_BUS_ABS ?= $(abspath $(FMC_BUS) )

GIT_VERSION = $(shell git describe --dirty --long --tags)
export GIT_VERSION

all modules:
	$(MAKE) -C $(LINUX) M=$(shell /bin/pwd) FMC_BUS_ABS=$(FMC_BUS_ABS) modules

install modules_install:
	$(MAKE) -C $(LINUX) M=$(shell /bin/pwd) modules_install

# be able to run the "clean" rule even if $(LINUX) is not valid
clean:
	rm -rf *.o *~  .*.cmd *.ko *.mod.c .tmp_versions Module.symvers \
		Module.markers modules.order
	rm -rf wr_nic/*.o wr_nic/*~ wr_nic/.*.o.cmd
