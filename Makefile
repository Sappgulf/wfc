CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra
LDLIBS = -lz

wfc: wfc.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

# sanitizer build: ./wfc with ASan+UBSan reporting
wfc_asan: wfc.c
	$(CC) -O1 -g -std=c11 -Wall -Wextra -fsanitize=address,undefined \
		-fno-omit-frame-pointer -o wfc_asan $< $(LDLIBS)

asan: wfc_asan

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

regression: wfc
	@set -e; \
	a=$$(./wfc --mode fire --seed 0 --w 8 --h 6 --once); \
	b=$$(./wfc --mode fire --seed 0 --w 8 --h 6 --once); \
	test "$$a" = "$$b"; \
	if ./wfc --w nope >/dev/null 2>/dev/null; then \
		echo 'regression: invalid --w was accepted' >&2; exit 1; \
	fi; \
	if ./wfc --mode fire --w 8 --h 6 --once --save /tmp >/dev/null 2>/dev/null; then \
		echo 'regression: failed save was reported as success' >&2; exit 1; \
	fi; \
	out=$$(mktemp -d /tmp/wfc-regression.XXXXXX); \
	./wfc --mode fire --seed 9 --w 8 --h 6 --once --save "$$out/map.png" >/dev/null; \
	test -s "$$out/map.png"; \
	echo 'regression: seed, argument, and export contracts OK'

python-check:
	@python3 -m py_compile wfc_learning.py wfc_thermo.py tests/fake_thermo.py \
		tests/test_wfc_learning.py tests/test_protocol_contract.py tests/test_c_bridge.py \
		tests/test_quality_studio.py tests/quality_benchmark.py tests/test_quality_benchmark.py
	@echo 'python: syntax OK'

protocol-check:
	@python3 -m unittest tests.test_protocol_contract -v

bridge-check: wfc
	@python3 -m unittest tests.test_c_bridge -v

learning-check:
	@python3 -m unittest tests.test_wfc_learning -v
	@echo "learning: profile/update contract OK"

quality-check: wfc
	@set -e; \
	for m in circuit streets neurons mycelium delta rail; do \
	out=$$(WFC_DEBUG=1 ./wfc --mode $$m --seed 7 --w 8 --h 6 --once 2>&1); \
		echo "$$out" | grep -q 'quality='; \
	done; \
	street=$$(WFC_DEBUG=1 ./wfc --mode streets --seed 7 --w 8 --h 6 --once 2>&1); \
	echo "$$street" | grep -q 'boundary=1.000'; \
	echo "quality: deterministic metrics and network borders OK"

studio-check: wfc
	@python3 -m unittest tests.test_quality_studio -v

studio-c-check:
	@cc -O2 -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
		-Wmissing-prototypes -Wcast-align -Wwrite-strings -o /tmp/wfc_studio_test tests/test_wfc_studio.c -lz
	@/tmp/wfc_studio_test

quality-benchmark: wfc
	@python3 tests/quality_benchmark.py --binary ./wfc --trials 1 --w 8 --h 6

# every mode against every render toggle, under ASan+UBSan
sweep: wfc_asan
	@set -e; fail=0; runs=0; \
	for m in $$(./wfc --list-modes); do \
		for opt in "" "--pan" "--zoom 4" "--no-bloom --no-weather" "--daycycle" "--w 7 --h 5"; do \
			runs=$$((runs + 1)); \
			ASAN_OPTIONS=detect_leaks=0 ./wfc_asan --mode $$m --seed 12345 \
				--w 24 --h 14 --once $$opt >/dev/null 2>/tmp/wfc_sweep_err.log || { \
				echo "FAIL: $$m $$opt"; head -14 /tmp/wfc_sweep_err.log; fail=1; }; \
			grep -qE 'ERROR|runtime error' /tmp/wfc_sweep_err.log && { \
				echo "SANITIZER: $$m $$opt"; head -14 /tmp/wfc_sweep_err.log; fail=1; }; \
		done; \
	done; \
	[ $$fail -eq 0 ] && echo "sweep: $$runs mode/option combos clean"

fuzz: wfc_asan
	@set -e; modes=$$(./wfc --list-modes); n=$$(echo "$$modes" | wc -l | tr -d ' '); \
	fail=0; \
	for i in $$(seq 1 25); do \
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
	[ $$fail -eq 0 ] && echo "fuzz: 25 random combos clean"

# everything a change has to survive before it lands
check: strict test regression python-check protocol-check bridge-check \
       learning-check quality-check studio-check studio-c-check \
       quality-benchmark sweep fuzz
	@echo "check: all suites clean"

clean:
	rm -rf wfc wfc_asan wfc_debug wfc_asan.dSYM

install: wfc
	install -m 0755 wfc /usr/local/bin/wfc

.PHONY: check clean install asan strict debug test regression python-check protocol-check bridge-check learning-check quality-check studio-check studio-c-check quality-benchmark sweep fuzz
