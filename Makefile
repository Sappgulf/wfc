CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra
LDLIBS = -lz
VERSION ?= 0.5.0
PREFIX ?= /usr/local
BUNDLE ?= dist/WFC.app
MACOS_ARCH := $(shell uname -m)

# wfc's main program still includes the world/render/UI parts in order. The
# filesystem, command, and solver seams are real separately compiled modules;
# listing both parts and modules keeps incremental rebuilds honest.
PARTS = wfc_world.h wfc_render.h wfc_export.h wfc_audio.h wfc_thermo.h wfc_ui.h
MODULES = wfc_artifact.c wfc_commands.c wfc_core.c wfc_mode.c wfc_platform.c \
	wfc_quality.c wfc_session.c wfc_solver.c
HEADERS = wfc_artifact.h wfc_commands.h wfc_core.h wfc_mode.h wfc_platform.h \
	wfc_quality.h wfc_session.h wfc_solver.h

wfc: wfc.c $(PARTS) $(MODULES) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $< $(MODULES) $(LDLIBS)

# sanitizer build: ./wfc with ASan+UBSan reporting
wfc_asan: wfc.c $(PARTS) $(MODULES) $(HEADERS)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -fsanitize=address,undefined \
		-fno-omit-frame-pointer -o wfc_asan $< $(MODULES) $(LDLIBS)

asan: wfc_asan

# pedantic warning sweep: must compile with zero output
strict: wfc.c $(PARTS) $(MODULES) $(HEADERS)
	$(CC) -O2 -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
		-Wmissing-prototypes -Wcast-align -Wwrite-strings -o /dev/null $< $(MODULES) $(LDLIBS)

debug: wfc.c $(PARTS) $(MODULES) $(HEADERS)
	$(CC) -O0 -g -std=c11 -Wall -Wextra -o wfc_debug $< $(MODULES) $(LDLIBS)

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
		tests/performance_gate.py tests/thermo_sweep.py tests/doctor.py \
		tests/test_graphics_audio.py tests/test_visual_regression.py tests/test_accelerated.py
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
		tests/test_wfc_studio.c $(MODULES) -lz
	@/tmp/wfc_studio_test

core-check:
	@cc -O2 -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
		-Wmissing-prototypes -Wcast-align -Wwrite-strings -o /tmp/wfc_core_test \
		tests/test_wfc_core.c wfc_core.c
	@/tmp/wfc_core_test
	@echo 'core: domain invariants OK'

quality-benchmark: wfc
	@python3 tests/quality_benchmark.py --binary ./wfc --trials 2 --w 8 --h 6

module-check:
	@cc -O2 -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
		-Wmissing-prototypes -Wcast-align -Wwrite-strings -o /tmp/wfc_module_test \
		tests/test_wfc_modules.c wfc_commands.c wfc_mode.c wfc_platform.c \
		wfc_quality.c wfc_solver.c -lm
	@/tmp/wfc_module_test
	@cc -O2 -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
		-Wmissing-prototypes -Wcast-align -Wwrite-strings -o /tmp/wfc_artifact_test \
		tests/test_wfc_artifact.c wfc_artifact.c
	@/tmp/wfc_artifact_test
	@cc -O2 -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
		-Wmissing-prototypes -Wcast-align -Wwrite-strings -o /tmp/wfc_session_test \
		tests/test_wfc_session.c wfc_session.c wfc_artifact.c
	@/tmp/wfc_session_test
	@echo 'modules: commands, sessions, artifacts, mode, platform, solver, and quality contracts OK'

doctor: wfc
	@python3 tests/doctor.py --binary ./wfc

graphics-smoke: wfc
	@python3 tests/test_graphics_audio.py --graphics --binary ./wfc

audio-smoke: wfc
	@python3 tests/test_graphics_audio.py --audio --binary ./wfc

visual-regression: wfc
	@python3 -m unittest tests.test_visual_regression -v

large-benchmark: wfc
	@./wfc --bench --w 48 --h 30 >/tmp/wfc-large-benchmark.txt
	@test "$$(wc -l </tmp/wfc-large-benchmark.txt | tr -d ' ')" -ge 38
	@tail -n 38 /tmp/wfc-large-benchmark.txt

thermo-env:
	@python3 -m venv .venv-thermo
	@.venv-thermo/bin/python -m pip install -r requirements-thermo.lock
	@echo 'thermo-env: ready; run ./wfc --solver thermo-accelerated'

macos-bundle: wfc
	@test "$$(uname -s)" = "Darwin" || { echo 'macos-bundle: macOS is required'; exit 2; }
	@rm -rf "$(BUNDLE)"
	@mkdir -p "$(BUNDLE)/Contents/MacOS" "$(BUNDLE)/Contents/Resources"
	@install -m 0755 wfc "$(BUNDLE)/Contents/MacOS/wfc"
	@install -m 0644 wfc_thermo.py wfc_learning.py "$(BUNDLE)/Contents/MacOS/"
	@install -m 0644 packaging/macos/Info.plist "$(BUNDLE)/Contents/Info.plist"
	@install -m 0644 README.md "$(BUNDLE)/Contents/Resources/README.md"
	@plutil -replace CFBundleShortVersionString -string "$(VERSION)" "$(BUNDLE)/Contents/Info.plist"
	@plutil -replace CFBundleVersion -string "$(VERSION)" "$(BUNDLE)/Contents/Info.plist"
	@if command -v codesign >/dev/null 2>&1; then codesign --force --deep --sign - "$(BUNDLE)" >/dev/null; fi
	@echo 'macos-bundle: $(BUNDLE) version $(VERSION) ready'

macos-tarball: macos-bundle
	@mkdir -p dist
	@tar -czf "dist/wfc-$(VERSION)-macos-$(MACOS_ARCH).tar.gz" -C dist WFC.app
	@echo 'macos-tarball: dist/wfc-$(VERSION)-macos-$(MACOS_ARCH).tar.gz ready'

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

accelerated-check: wfc
	@python3 -m unittest tests.test_accelerated -v

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

# fast feedback: compile, deterministic contracts, and focused unit suites
check-fast: strict test regression python-check protocol-check bridge-check \
	   learning-check studio-c-check module-check core-check docs-check cli-check
	@echo "check-fast: all suites clean"

# full release gate: fast feedback plus quality, interactive, sanitizer,
# graphics/audio contracts, and a larger 48x30 benchmark.
check-full: check-fast quality-check studio-check thermo-check interactive-check \
	   interactive-asan-check performance-check perf-check quality-benchmark \
	   large-benchmark graphics-smoke audio-smoke visual-regression accelerated-check sweep fuzz doctor
	@echo "check-full: all suites clean"

# Backwards-compatible default for local contributors and CI callers.
check: check-full
	@echo "check: all suites clean"

clean:
	rm -rf wfc wfc_asan wfc_debug wfc_asan.dSYM

install: wfc
	install -d "$(DESTDIR)$(PREFIX)/bin"
	install -m 0755 wfc "$(DESTDIR)$(PREFIX)/bin/wfc"

.PHONY: check check-fast check-full clean install asan strict debug test regression \
	python-check protocol-check bridge-check learning-check quality-check studio-check \
	studio-c-check module-check core-check docs-check cli-check thermo-check \
	interactive-check interactive-asan-check performance-check perf-check \
	quality-benchmark large-benchmark thermo-env graphics-smoke audio-smoke visual-regression accelerated-check doctor sweep fuzz \
	macos-bundle macos-tarball
