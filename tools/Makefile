# If it exists includes Makefile.specific. In this Makefile, you should put
# specific Makefile code that you want to run before this. For example,
# build a particular environment.
-include Makefile.specific

CFLAGS += -ggdb -Wall -fPIC -I../kernel $(EXTRACFLAGS)
LDFLAGS += -L. -lspec

LIB = libspec.a libflash/libflash.a
LIBOBJ = speclib.o loader-ll.o libflash/flash.o

LIBSHARED = libspec.so

PROGS = spec-cl spec-fwloader spec-vuart specmem
PROGS += wr-dio-cmd wr-dio-pps wr-dio-agent wr-dio-ruler
PROGS += stamp-frame flash-write flash-read

all: $(LIB) $(PROGS) $(LIBSHARED)

$(PROGS): $(LIB)

$(LIB): $(LIBOBJ)
	ar r $@ $^

loader-ll.o:	../kernel/loader-ll.c
		${CC} $(CFLAGS) -c $^ -I .

$(LIBSHARED): libspec.a
	${CC} -shared -o $@ -Wl,--whole-archive $^ -Wl,--no-whole-archive


clean:
	rm -f *.o $(LIB) $(LIBOBJ) $(PROGS) $(LIBSHARED) *~

# add the other unused targets, so the rule in ../Makefile works
modules install modules_install:
