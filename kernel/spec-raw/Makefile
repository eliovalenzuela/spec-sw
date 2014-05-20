
LINUX ?= /lib/modules/$(shell uname -r)/build

ccflags-y += -I$(src)/../../fmc-bus/kernel/include
ccflags-$(CONFIG_SPEC_RAW_DEBUG) += -DDEBUG

obj-m = spec-raw.o
spec-raw-y := spec-raw-core.o
spec-raw-y += spec-raw-dma.o
spec-raw-y += spec-raw-chardev.o
spec-raw-y += spec-raw-irq.o
spec-raw-y += spec-raw-dbg.o
spec-raw-y += spec-raw-sysfs.o

GIT_VERSION = $(shell cd $(src); git describe --dirty --long --tags)

ccflags-y += -DGIT_VERSION=\"$(GIT_VERSION)\"

all modules:
	$(MAKE) -C $(LINUX) M=$(shell /bin/pwd) modules

install modules_install:
	$(MAKE) -C $(LINUX) M=$(shell /bin/pwd) modules_install

# be able to run the "clean" rule even if $(LINUX) is not valid
clean:
	rm -rf *.o *~  .*.cmd *.ko *.mod.c .tmp_versions Module.symvers \
		Module.markers modules.order
