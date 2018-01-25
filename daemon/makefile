.PHONY = test
CFLAGS ?= -g -Wall
LDLIBS = -lX11

OBJECTS = $(patsubst %.c,%.o,$(wildcard *.c ../libs/easy_json.c))

rpcd: $(OBJECTS)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

all: rpcd

clean:
	$(RM) $(OBJECTS)
	$(RM) rpcd

test:
	valgrind --leak-check=full --show-leak-kinds=all ./rpcd rpcd.conf
