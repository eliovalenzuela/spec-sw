# If it exists includes Makefile.specific. In this Makefile, you should put
# specific Makefile code that you want to run before this. For example,
# build a particular environment.
-include Makefile.specific

# include parent_common.mk for buildsystem's defines
REPO_PARENT=../..
-include $(REPO_PARENT)/parent_common.mk

GIT_VERSION ?= $(shell git describe --dirty --long --tags)
CFLAGS += -ggdb -Wall -fPIC -I../kernel $(EXTRACFLAGS)
CFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"

INSTALL = install
DESTDIR =
PREFIX  = /usr/local

BINDIR = $(PREFIX)/sbin
LIBDIR = $(PREFIX)/lib

LIB = libspec.a
LIBOBJ = speclib.o loader-ll.o

LIBSHARED = libspec.so

PROGS = spec-cl spec-fwloader spec-vuart specmem
PROGS += wr-dio-cmd wr-dio-pps wr-dio-agent wr-dio-ruler
PROGS += stamp-frame

all: $(LIB) $(PROGS) $(LIBSHARED)

$(PROGS): $(LIB)

$(LIB): $(LIBOBJ)
	$(AR) r $@ $^

loader-ll.o:	../kernel/loader-ll.c
		${CC} $(CFLAGS) -c $^ -I .

$(LIBSHARED): $(LIB)
	$(CC) -shared -o $@ -Wl,--whole-archive $^ -Wl,--no-whole-archive

install:
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 0755 -t $(DESTDIR)$(BINDIR) $(PROGS)
	$(INSTALL) -d $(DESTDIR)$(LIBDIR)
	$(INSTALL) -m 0755 -t $(DESTDIR)$(LIBDIR) $(LIB)
	$(INSTALL) -m 0755 -t $(DESTDIR)$(LIBDIR) $(LIBOBJ)
	$(INSTALL) -m 0755 -t $(DESTDIR)$(LIBDIR) $(LIBSHARED)

clean:
	rm -f *.o $(LIB) $(PROGS) $(LIBSHARED) *~

# add the other unused targets, so the rule in ../Makefile works
modules modules_install:
