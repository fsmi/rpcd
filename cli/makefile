.PHONY: clean install

DESTDIR=/
INSTALL_DIR=usr/local/bin
CFLAGS=-Wall
LDFLAGS=-lcurl

OBJECTS= $(patsubst %.c, %.o, $(wildcard *.c ../libs/*.c))
DEPS= $(wildcard *.h ../libs/*.h)

all: rpcd-cli

debug: CFLAGS += -g
debug: rpcd-cli

rpcd-cli: $(OBJECTS)

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) $(LDFLAGS) -c -o $@ $<


install:
	install -m 0755 -D rpcd-cli $(DESTDIR)$(INSTALL_DIR)/rpcd-cli

clean:
	$(RM) $(OBJECTS)
	$(RM) rpcd-cli
