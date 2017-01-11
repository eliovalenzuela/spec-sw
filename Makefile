# include parent_common.mk for buildsystem's defines
#use absolute path for REPO_PARENT
REPO_PARENT=$(shell /bin/pwd)/..
-include $(REPO_PARENT)/parent_common.mk

# export git tree for subdirs
GIT_VERSION ?= $(shell git describe --dirty --long --tags)
export GIT_VERSION

# by default use the fmc-bus within the repository
FMC_BUS ?= fmc-bus
# FMC_BUS_ABS has to be absolut path, due to beeing passed to the Kbuild
FMC_BUS_ABS ?= $(abspath $(FMC_BUS) )
export FMC_BUS_ABS

DIRS = $(FMC_BUS_ABS) binaries kernel tools

.PHONY: all clean modules install modules_install $(DIRS)

all clean modules install modules_install: $(DIRS)

clean: TARGET = clean
modules: TARGET = modules
install: TARGET = install
modules_install: TARGET = modules_install

$(DIRS):
	$(MAKE) -C $@ $(TARGET)

$(FMC_BUS_ABS): fmc-bus-init_repo

# init submodule if missing
fmc-bus-init_repo:
	@test -d $(FMC_BUS_ABS)/doc || ( echo "Checking out submodule $(FMC_BUS_ABS)"; git submodule update --init $(FMC_BUS_ABS) )

kernel: $(FMC_BUS_ABS)
