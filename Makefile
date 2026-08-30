CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra
LDLIBS = -lz

wfc: wfc.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

# sanitizer build: ./wfc with ASan+UBSan reporting
asan: wfc.c
	$(CC) -O1 -g -std=c11 -Wall -Wextra -fsanitize=address,undefined \
		-fno-omit-frame-pointer -o wfc_asan $< $(LDLIBS)

# pedantic warning sweep: must compile with zero output
strict: wfc.c
	$(CC) -O2 -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
		-Wmissing-prototypes -Wcast-align -Wwrite-strings -o /dev/null $< $(LDLIBS)

debug: wfc.c
	$(CC) -O0 -g -std=c11 -Wall -Wextra -o wfc_debug $< $(LDLIBS)

test: wfc
	@set -e; for m in $$(./wfc --list-modes); do \
		./wfc --mode $$m --w 40 --h 20 --once >/dev/null; \
		printf '  %-9s ok\n' $$m; \
	done
	@./wfc --collage /tmp/wfc_test_collage.png >/dev/null
	@echo "$$(./wfc --list-modes | wc -l | tr -d ' ') modes + collage OK"

fuzz: wfc_asan
	@set -e; modes=$$(./wfc --list-modes); n=$$(echo "$$modes" | wc -l | tr -d ' '); \
	fail=0; \
	for i in $$(seq 1 24); do \
		m=$$(echo "$$modes" | sed -n $$((RANDOM % $$n + 1))p); \
		s=$$((RANDOM * 32768 + RANDOM)); \
		w=$$((RANDOM % 90 + 6)); h=$$((RANDOM % 40 + 6)); \
		extra=""; \
		[ $$((RANDOM % 3)) -eq 0 ] && extra="$$extra --pan"; \
		[ $$((RANDOM % 3)) -eq 0 ] && extra="$$extra --no-bloom"; \
		[ $$((RANDOM % 3)) -eq 0 ] && extra="$$extra --no-weather"; \
		[ $$((RANDOM % 4)) -eq 0 ] && extra="$$extra --zoom 3"; \
		ASAN_OPTIONS=detect_leaks=0 ./wfc_asan --mode $$m --seed $$s --w $$w --h $$h \
			--once $$extra >/dev/null 2>/tmp/wfc_fuzz_err.log || { \
			echo "FAIL: $$m seed=$$s $${w}x$$h$$extra"; cat /tmp/wfc_fuzz_err.log; fail=1; break; }; \
	done; \
	[ $$fail -eq 0 ] && echo "fuzz: 24 random combos clean"

clean:
	rm -rf wfc wfc_asan wfc_debug wfc_asan.dSYM

install: wfc
	install -m 0755 wfc /usr/local/bin/wfc

.PHONY: clean install asan strict debug test fuzz
