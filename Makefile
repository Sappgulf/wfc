CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra
LDLIBS = -lz

# wfc's main program is one translation unit: wfc.c includes these parts in
# order. wfc_core.c is the small independent invariant module. The files are
# listed here so editing one triggers a rebuild.
PARTS = wfc_world.h wfc_render.h wfc_export.h wfc_audio.h wfc_thermo.h wfc_ui.h
CORE = wfc_core.c wfc_core.h

wfc: wfc.c $(PARTS) $(CORE)
	$(CC) $(CFLAGS) -o $@ $< wfc_core.c $(LDLIBS)

# sanitizer build: ./wfc with ASan+UBSan reporting
wfc_asan: wfc.c $(PARTS) $(CORE)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -fsanitize=address,undefined \
		-fno-omit-frame-pointer -o wfc_asan $< wfc_core.c $(LDLIBS)

asan: wfc_asan

# pedantic warning sweep: must compile with zero output
strict: wfc.c $(PARTS) $(CORE)
	$(CC) -O2 -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
		-Wmissing-prototypes -Wcast-align -Wwrite-strings -o /dev/null $< wfc_core.c $(LDLIBS)

debug: wfc.c $(PARTS) $(CORE)
	$(CC) -O0 -g -std=c11 -Wall -Wextra -o wfc_debug $< wfc_core.c $(LDLIBS)

test: wfc
	@set -e; for m in $$(./wfc --list-modes); do \
		./wfc --mode $$m --seed 4242 --w 40 --h 20 --once >/dev/null; \
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
	for m in terrain rail geode storm stained; do \
		./wfc --mode $$m --seed 4242 --w 24 --h 12 --once --save /tmp/wfc-rep-a.png >/dev/null; \
		./wfc --mode $$m --seed 4242 --w 24 --h 12 --once --save /tmp/wfc-rep-b.png >/dev/null; \
		cmp -s /tmp/wfc-rep-a.png /tmp/wfc-rep-b.png || { \
			echo "regression: $$m export is not reproducible" >&2; exit 1; }; \
	done; \
	out=$$(mktemp -d /tmp/wfc-regression.XXXXXX); \
	./wfc --mode fire --seed 9 --w 8 --h 6 --once --save "$$out/map.png" >/dev/null; \
	test -s "$$out/map.png"; \
	echo 'regression: seed, argument, and export contracts OK'

python-check:
	@python3 -m py_compile wfc_learning.py wfc_thermo.py tests/fake_thermo.py \
		tests/test_wfc_learning.py tests/test_protocol_contract.py tests/test_c_bridge.py \
		tests/test_quality_studio.py tests/quality_benchmark.py tests/test_quality_benchmark.py \
		tests/test_interactive.py tests/test_docs.py tests/test_fuzz_headless.py \
		tests/test_cli_features.py tests/test_performance_gate.py tests/fuzz_headless.py \
		tests/performance_gate.py tests/thermo_sweep.py
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

# the live TUI, driven through a pty: keys, overlays, escape sequences
interactive-check: wfc
	@python3 -m unittest tests.test_interactive -v

studio-c-check:
	@cc -O2 -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
		-Wmissing-prototypes -Wcast-align -Wwrite-strings -o /tmp/wfc_studio_test \
		tests/test_wfc_studio.c wfc_core.c -lz
	@/tmp/wfc_studio_test

core-check:
	@cc -O2 -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
		-Wmissing-prototypes -Wcast-align -Wwrite-strings -o /tmp/wfc_core_test \
		tests/test_wfc_core.c wfc_core.c
	@/tmp/wfc_core_test
	@echo 'core: domain invariants OK'

quality-benchmark: wfc
	@python3 tests/quality_benchmark.py --binary ./wfc --trials 2 --w 8 --h 6

performance-check: wfc
	@python3 -m unittest tests.test_quality_benchmark tests.test_performance_gate -v

perf-check: wfc
	@python3 tests/performance_gate.py --binary ./wfc \
		--budget tests/performance_budget.json --trials 2 --w 8 --h 6

docs-check: wfc
	@python3 -m unittest tests.test_docs -v

cli-check: wfc
	@python3 -m unittest tests.test_cli_features -v

thermo-check: wfc wfc_asan
	@python3 tests/thermo_sweep.py --binary ./wfc_asan --worker ./tests/fake_thermo.py

interactive-asan-check: wfc_asan
	@WFC_BINARY=./wfc_asan ASAN_OPTIONS=detect_leaks=0 \
		python3 -m unittest tests.test_interactive -v

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

fuzz: wfc wfc_asan
	@python3 tests/fuzz_headless.py --binary ./wfc_asan --runs 50 --seed 20260831

# everything a change has to survive before it lands
check: strict test regression python-check protocol-check bridge-check \
	   learning-check quality-check studio-check studio-c-check \
	   core-check docs-check cli-check thermo-check interactive-check \
	   interactive-asan-check performance-check perf-check quality-benchmark sweep fuzz
	@echo "check: all suites clean"

clean:
	rm -rf wfc wfc_asan wfc_debug wfc_asan.dSYM

install: wfc
	install -m 0755 wfc /usr/local/bin/wfc

.PHONY: check clean install asan strict debug test regression python-check protocol-check bridge-check learning-check quality-check studio-check studio-c-check core-check docs-check cli-check thermo-check interactive-check interactive-asan-check performance-check perf-check quality-benchmark sweep fuzz
