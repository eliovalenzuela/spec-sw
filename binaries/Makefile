# If it exists includes Makefile.specific. In this Makefile, you should put
# specific Makefile code that you want to run before this. For example,
# build a particular environment.
-include Makefile.specific

# include parent_common.mk for buildsystem's defines
REPO_PARENT=../..
-include $(REPO_PARENT)/parent_common.mk

INSTALL = install
DESTDIR =
PREFIX  = /lib/

FIRMDIR = $(PREFIX)/firmware/spec-sw

FIRMWARE = fmc-dio-eeprom.bin

all: $(FIRMWARE)

$(FIRMWARE):
	ls

install:
	$(INSTALL) -d $(DESTDIR)$(FIRMDIR)
	$(INSTALL) -m 0644 -t $(DESTDIR)$(FIRMDIR) $(FIRMWARE)

# add the other unused targets, so the rule in ../Makefile works
modules modules_install clean:
