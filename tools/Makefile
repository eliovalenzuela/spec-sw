
CFLAGS = -ggdb -Wall -I../kernel
LDFLAGS = -L. -lspec

LIB = libspec.a
LIBOBJ = speclib.o loader-ll.o

PROGS = spec-cl spec-fwloader spec-vuart specmem
PROGS += wr-dio-cmd wr-dio-pps wr-dio-agent wr-dio-ruler
PROGS += stamp-frame


all: $(LIB) $(PROGS)

$(PROGS): $(LIB)

$(LIB): $(LIBOBJ)
	ar r $@ $^

loader-ll.o:	../kernel/loader-ll.c
		${CC} -c $^ -I .

clean:
	rm -f *.o $(LIB) $(PROGS) *~

# add the other unused targets, so the rule in ../Makefile works
modules install modules_install:
