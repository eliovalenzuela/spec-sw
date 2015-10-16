# include parent_common.mk for buildsystem's defines
#use absolute path for REPO_PARENT
REPO_PARENT=$(shell /bin/pwd)/..
-include $(REPO_PARENT)/parent_common.mk

# by default use the fmc-bus within the repository
FMC_BUS ?= $(shell pwd)/fmc-bus/
export FMC_BUS
FMC_DRV ?= $(FMC_BUS)/kernel/
export FMC_DRV

RUNME := $(shell test -d $(FMC_DRV) || git submodule update --init)

DIRS = $(FMC_BUS) kernel tools

all clean modules install modules_install:
	for d in $(DIRS); do $(MAKE) -C $$d $@ || exit 1; done
