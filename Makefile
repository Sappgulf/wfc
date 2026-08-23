CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra

wfc: wfc.c
	$(CC) $(CFLAGS) -o $@ $< -lz

clean:
	rm -f wfc

install: wfc
	install -m 0755 wfc /usr/local/bin/wfc

.PHONY: clean install
