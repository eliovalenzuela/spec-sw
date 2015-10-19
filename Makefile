# include parent_common.mk for buildsystem's defines
#use absolute path for REPO_PARENT
REPO_PARENT=$(shell /bin/pwd)/..
-include $(REPO_PARENT)/parent_common.mk

# by default use the fmc-bus within the repository
FMC_BUS ?= $(shell pwd)/fmc-bus/
export FMC_BUS

RUNME := $(shell test -d $(FMC_DRV) || git submodule update --init)

DIRS = $(FMC_BUS) kernel tools

.PHONY: all clean modules install modules_install $(DIRS)

all clean modules install modules_install: $(DIRS)

clean: TARGET = clean
modules: TARGET = modules
install: TARGET = install
modules_install: TARGET = modules_install

$(DIRS):
	$(MAKE) -C $@ $(TARGET)

kernel: $(FMC_BUS)
