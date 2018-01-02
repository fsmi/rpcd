.PHONY: clean install debug

PREFIX ?= /usr/local
CFLAGS ?= -Wall -g
LDLIBS = -lcurl

OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c ../libs/*.c))
DEPS = $(wildcard *.h ../libs/*.h)

all: rpcd-cli rpcd-cli.1.gz

rpcd-cli: $(OBJECTS)

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) $(LDFLAGS) -c -o $@ $<

%.1.gz : %.1
	gzip -c < $< > $@

install:
	install -m 0755 -D rpcd-cli "$(DESTDIR)$(PREFIX)/bin"
	install -m 0644 -g 0 -o 0 -D rpcd-cli.1.gz $(DESTDIR)$(PREFIX)/share/man/man1

clean:
	$(RM) $(OBJECTS)
	$(RM) rpcd-cli
	$(RM) rpcd-cli.1.gz
