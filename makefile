.PHONY: all clean

all:
	$(MAKE) -C daemon
	$(MAKE) -C cli

clean:
	$(MAKE) -C daemon clean
	$(MAKE) -C cli clean
