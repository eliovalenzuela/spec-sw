
CFLAGS = -ggdb -Wall -I../kernel
LDFLAGS = -L. -lspec

LIB = libspec.a
LIBOBJ = speclib.o loader-ll.o

PROGS = spec-cl spec-fwloader spec-vuart specmem
PROGS += wr-dio-cmd


all: $(LIB) $(PROGS)

$(PROGS): $(LIB)

$(LIB): $(LIBOBJ)
	ar r $@ $^

loader-ll.o:	../kernel/loader-ll.c
		${CC} -c $^ -I .

clean:
	rm -f *.o $(LIB) $(PROGS) *~