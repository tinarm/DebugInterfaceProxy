PREFIX?=/
CC?=gcc

CFLAGS+=-O -Wall -g

LDFLAGS+=-lpthread

BINARIES=debug_interface_proxy

#-----------------------------------------------------------------------

all: $(BINARIES)

install: $(BINARIES)
#	mkdir -p $(PREFIX)/sbin
#	install -m 755 $(BINARIES) $(PREFIX)/sbin
#	mkdir -p $(PREFIX)/sbin
#	install -m 755 $(BINARIES) $(PREFIX)/sbin

clean:
	rm -f $(BINARIES) core *.o

debug_interface_proxy: main.o cmdserver.o utils.o tracecmd.o mldproc.o autoconf.o
	$(CC) $^ $(LDFLAGS) -o $@ $(LIB)

%.o: %.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $^ -o $(@)
