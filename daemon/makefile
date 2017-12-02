CFLAGS ?= -g -Wall
LDLIBS = -lX11

OBJECTS = $(patsubst %.c,%.o,$(wildcard *.c))

rpcd: $(OBJECTS)

all: rpcd
