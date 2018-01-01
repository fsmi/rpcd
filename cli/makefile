.PHONY: clean install debug

PREFIX ?= /usr/local
CFLAGS ?= -Wall -g
LDLIBS = -lcurl

OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c ../libs/*.c))
DEPS = $(wildcard *.h ../libs/*.h)

all: rpcd-cli

rpcd-cli: $(OBJECTS)

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) $(LDFLAGS) -c -o $@ $<

install:
	install -m 0755 -D rpcd-cli "$(DESTDIR)$(PREFIX)/bin"

clean:
	$(RM) $(OBJECTS)
	$(RM) rpcd-cli
