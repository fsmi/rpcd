.PHONY: all clean

all:
	$(MAKE) -C daemon

clean:
	$(MAKE) -C daemon clean
