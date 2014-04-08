
CFLAGS = -ggdb -Wall -fPIC -I../kernel
LDFLAGS = -L. -lspec

LIB = libspec.a
LIBOBJ = speclib.o loader-ll.o

LIBSHARED = libspec.so

PROGS = spec-cl spec-fwloader spec-vuart specmem
PROGS += wr-dio-cmd wr-dio-pps wr-dio-agent wr-dio-ruler
PROGS += stamp-frame

all: $(LIB) $(PROGS) $(LIBSHARED)

$(PROGS): $(LIB)

$(LIB): $(LIBOBJ)
	ar r $@ $^

loader-ll.o:	../kernel/loader-ll.c
		${CC} -fPIC -c $^ -I .

$(LIBSHARED): $(LIB)
	${CC} -shared -o $@ -Wl,--whole-archive $^ -Wl,--no-whole-archive

clean:
	rm -f *.o $(LIB) $(PROGS) $(LIBSHARED) *~

# add the other unused targets, so the rule in ../Makefile works
modules install modules_install:
