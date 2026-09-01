import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parent))
import sandbox  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "wfc"


class QualityStudioTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run(["make"], cwd=ROOT, check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    def run_wfc(self, *args, env=None):
        merged = sandbox.env(**(env or {}))
        return subprocess.run([os.fspath(BINARY), *args], cwd=ROOT,
                              env=merged, capture_output=True, text=True,
                              timeout=10)

    def test_registry_lists_unique_named_worlds(self):
        result = self.run_wfc("--list-modes")
        self.assertEqual(result.returncode, 0, result.stderr)
        modes = result.stdout.splitlines()
        self.assertEqual(len(modes), len(set(modes)), "duplicate mode name")
        self.assertTrue(set(modes) >= {
            "circuit", "terrain", "delta", "storm", "glacier",
            "bamboo", "solar", "rail",
        })

    def test_new_field_modes_are_registered_as_a_complete_family(self):
        """The next visual family must be discoverable before it is rendered."""
        modes = self.run_wfc("--list-modes").stdout.splitlines()
        self.assertEqual(len(modes), 37)
        self.assertTrue(set(modes) >= {"tide", "marble", "cinder", "origami"})

    def test_rail_uses_topology_quality_instead_of_the_neutral_default(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            report = Path(temp_dir) / "rail.json"
            result = self.run_wfc(
                "--mode", "rail", "--seed", "7", "--w", "8", "--h", "6",
                "--once", "--report", os.fspath(report),
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            payload = json.loads(report.read_text(encoding="utf-8"))
        self.assertLess(payload["quality"]["topology"], 0.999999,
                         "rail must not receive the neutral topology score")

    def test_report_contains_reproducibility_quality_thermo_and_studio(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            report = Path(temp_dir) / "world.json"
            result = self.run_wfc(
                "--mode", "delta", "--seed", "7", "--w", "8", "--h", "6",
                "--once", "--report", os.fspath(report),
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            payload = json.loads(report.read_text(encoding="utf-8"))
            self.assertEqual(payload["schema"], 2)
            self.assertEqual(payload["mode"], "delta")
            self.assertEqual(payload["seed"], 7)
            self.assertEqual(payload["dimensions"], {"w": 8, "h": 6})
            self.assertEqual(payload["solver"], "classic")
            self.assertEqual(payload["backend"], "classic")
            self.assertEqual(payload["thermo"]["backend"], "classic")
            self.assertEqual(payload["quality"]["focus"], "delta")
            self.assertEqual(set(payload["quality"]["profile_weights"]), {
                "validity", "boundary", "coverage", "diversity", "smoothness",
                "stability", "topology",
            })
            hotspot = payload["quality"]["hotspot"]
            self.assertIn(hotspot["reason"], {"entropy", "boundary", "coverage",
                                                "branch", "validity", "balanced"})
            self.assertGreaterEqual(hotspot["score"], 0.0)
            self.assertLessEqual(hotspot["score"], 1.0)
            for key in ("total", "validity", "boundary", "coverage", "diversity",
                        "smoothness", "stability", "topology"):
                self.assertIn(key, payload["quality"])
                self.assertGreaterEqual(payload["quality"][key], 0.0)
                self.assertLessEqual(payload["quality"][key], 1.0)
            for key in ("enabled", "sampler", "observations", "proposals",
                        "accepted", "rejected", "contradictions"):
                self.assertIn(key, payload["thermo"])
            self.assertEqual(payload["studio"]["pins"], 0)
            self.assertEqual(payload["macro"]["name"], "delta-channel")
            self.assertGreater(payload["macro"]["guided_cells"], 0)
            self.assertEqual(payload["evolution"]["candidates"], 0)

    def test_fullscreen_flag_keeps_headless_dimensions_deterministic(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            report = Path(temp_dir) / "headless-fullscreen.json"
            result = self.run_wfc(
                "--fullscreen", "--mode", "delta", "--seed", "7",
                "--w", "8", "--h", "6", "--once", "--report",
                os.fspath(report),
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            payload = json.loads(report.read_text(encoding="utf-8"))
            self.assertEqual(payload["dimensions"], {"w": 8, "h": 6})

    def test_evolution_lab_is_reproducible_and_reports_a_winner(self):
        args = ("--mode", "delta", "--seed", "7", "--w", "8", "--h", "6",
                "--once", "--evolve", "3")
        first = self.run_wfc(*args)
        second = self.run_wfc(*args)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertEqual(first.stdout, second.stdout)
        self.assertIn("evolution candidates=3", first.stdout)
        self.assertRegex(first.stdout, r"winner_seed=\d+ quality=0\.\d+")

    def test_debug_quality_identifies_delta_profile(self):
        result = self.run_wfc(
            "--mode", "delta", "--seed", "7", "--w", "8", "--h", "6", "--once",
            env={"WFC_DEBUG": "1"},
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("quality=", result.stderr)
        self.assertIn("focus=delta", result.stderr)

    def test_help_surfaces_observatory_studio_and_report_controls(self):
        result = self.run_wfc("--help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--report FILE", result.stdout)
        self.assertIn("--world-file FILE", result.stdout)
        self.assertIn("--session FILE", result.stdout)
        self.assertIn("--inspect-world FILE", result.stdout)
        self.assertIn("l observatory", result.stdout)
        self.assertIn("P pin", result.stdout)
        self.assertIn("Q heatmap", result.stdout)
        self.assertIn("E evolution", result.stdout)
        self.assertIn("x repair hotspot", result.stdout)
        self.assertIn("--evolve N", result.stdout)
        self.assertIn("--fullscreen", result.stdout)
        self.assertIn("/ pick world", result.stdout)
        self.assertIn("[ ] density", result.stdout)

    def test_every_registered_mode_solves_and_exports(self):
        """Each row in MODESPEC[] must solve headlessly and survive an export.

        A new world is a table row plus a render branch; this is the gate that
        catches a row whose branch was never wired into the renderers.
        """
        modes = self.run_wfc("--list-modes").stdout.split()
        self.assertGreaterEqual(len(modes), 30)
        with tempfile.TemporaryDirectory() as tmp:
            for mode in modes:
                png = Path(tmp) / ("%s.png" % mode)
                report = Path(tmp) / ("%s.json" % mode)
                result = self.run_wfc(
                    "--mode", mode, "--seed", "31337", "--w", "14", "--h", "9",
                    "--once", "--save", os.fspath(png),
                    "--report", os.fspath(report))
                self.assertEqual(result.returncode, 0,
                                 "%s: %s" % (mode, result.stderr))
                self.assertGreater(png.stat().st_size, 0, mode)
                payload = json.loads(report.read_text(encoding="utf-8"))
                self.assertEqual(payload["mode"], mode)
                self.assertGreater(payload["quality"]["total"], 0.0, mode)

    def test_network_worlds_all_carry_macro_guidance(self):
        """Every world flagged `network` in the registry must guide cells.

        rail joined the family late; without a macro_role_at() arm it would
        report zero guided cells and quietly fall back to ungrided WFC.
        """
        with tempfile.TemporaryDirectory() as tmp:
            for mode in ("streets", "neurons", "mycelium", "delta", "rail"):
                report = Path(tmp) / ("%s.json" % mode)
                result = self.run_wfc("--mode", mode, "--seed", "7",
                                      "--w", "24", "--h", "14", "--once",
                                      "--report", os.fspath(report))
                self.assertEqual(result.returncode, 0, result.stderr)
                payload = json.loads(report.read_text(encoding="utf-8"))
                self.assertNotEqual(payload["macro"]["name"], "none", mode)
                self.assertGreater(payload["macro"]["guided_cells"], 0, mode)

    def test_modes_flag_prints_a_blurb_for_every_world(self):
        """--modes renders the registry, so the CLI cannot drift from it."""
        names = self.run_wfc("--list-modes").stdout.split()
        result = self.run_wfc("--modes")
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = [l for l in result.stdout.splitlines() if l.strip()]
        self.assertEqual(len(lines), len(names))
        for name, line in zip(names, lines):
            self.assertTrue(line.startswith(name), line)
            self.assertGreater(len(line[len(name):].strip()), 8, "blurb missing")

    def test_every_world_has_its_own_key_and_family(self):
        """Sound is derived from (key, family); a shared pair means twins.

        Two hand-kept tables of twenty-five used to drive the stingers and
        drones, indexed `mode % 25`, so the eight worlds past the twenty-fifth
        played another world's music outright.
        """
        rows = [l.split() for l in self.run_wfc("--modes").stdout.splitlines() if l.strip()]
        names = self.run_wfc("--list-modes").stdout.split()
        self.assertEqual(len(rows), len(names))
        seen = {}
        for row in rows:
            name, family, key = row[0], row[1], row[2]
            self.assertIn(family, {"field", "connector", "carve"}, name)
            self.assertRegex(key, r"^[A-G]#?[0-9]$", name)
            self.assertNotIn((family, key), seen,
                             "%s sounds like %s" % (name, seen.get((family, key))))
            seen[(family, key)] = name

    def _report(self, tmp, *extra):
        report = Path(tmp) / "r.json"
        result = self.run_wfc("--mode", "streets", "--seed", "31337",
                              "--w", "20", "--h", "12", "--once",
                              "--thermo-profile", os.fspath(tmp),
                              "--report", os.fspath(report), *extra)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(report.read_text(encoding="utf-8"))["quality"]["total"]

    def test_learned_profile_steers_the_classic_solver(self):
        """--learned applies the sidecar's tile preferences without it running.

        The worker learned which tiles pay off and wrote them to a profile,
        but only the worker ever read them back: --solver classic left every
        lesson sitting unused on disk.
        """
        with tempfile.TemporaryDirectory() as tmp:
            plain = self._report(tmp)
            self.assertAlmostEqual(plain, self._report(tmp, "--learned"), places=9,
                                   msg="--learned with no profile must change nothing")

            # a lopsided but well-formed profile for the 14-tile connector set
            bias = [2.0] + [-2.0] * 13
            (Path(tmp) / "streets-deadbeef.json").write_text(
                json.dumps({"mode": "streets", "tile_bias": bias}), encoding="utf-8")
            steered = self._report(tmp, "--learned")
            self.assertNotAlmostEqual(plain, steered, places=6,
                                      msg="a profile must reach the classic pick")

    def test_context_bias_alone_changes_the_pick(self):
        """A context preference has to be able to act, or it is dead weight.

        context_bias was added identically to every tile's score at a cell, so
        it cancelled in the softmax over that cell's options: learned,
        persisted and transmitted, and unable to change any decision. It now
        acts through how permissive each tile is, so it can say "here, lean
        open" — and this asserts a profile carrying only that still bites.
        """
        with tempfile.TemporaryDirectory() as tmp:
            plain = self._report(tmp)
            (Path(tmp) / "streets-deadbeef.json").write_text(
                json.dumps({"mode": "streets", "context_bias": [2.0] * 8}),
                encoding="utf-8")
            self.assertNotAlmostEqual(plain, self._report(tmp, "--learned"), places=6,
                                      msg="context bias must reach the pick")

    def test_a_profile_that_does_not_fit_the_tileset_is_ignored(self):
        """Length is the guard: the fingerprint lives on the Python side."""
        with tempfile.TemporaryDirectory() as tmp:
            plain = self._report(tmp)
            for bad in ({"tile_bias": [0.5] * 3},                    # too short
                        {"tile_bias": [0.5] * 40},                   # too long
                        {"tile_bias": [99.0] * 14},                  # out of range
                        {"tile_bias": "not-an-array"},
                        {}):
                (Path(tmp) / "streets-deadbeef.json").write_text(
                    json.dumps(bad), encoding="utf-8")
                self.assertAlmostEqual(plain, self._report(tmp, "--learned"), places=9,
                                       msg="malformed profile must be ignored: %s" % bad)

    def test_colour_assist_separates_red_from_green(self):
        """The assist has to help the viewer it is for, measurably.

        Textbook daltonization moved the pairs that carry meaning 35% closer
        together under a deuteranopia simulation, because it redistributes
        green error back into green. Moving the red-green signal onto the
        blue axis instead separates them. This asserts the direction.
        """
        def simulate(rgb):                     # what a deuteranope receives
            r, g, b = rgb
            long_ = 17.8824 * r + 43.5161 * g + 4.11935 * b
            short = 0.0299566 * r + 0.184309 * g + 1.46709 * b
            mid = 0.494207 * long_ + 1.24827 * short
            return (0.0809444479 * long_ - 0.130504409 * mid + 0.116721066 * short,
                    -0.0102485335 * long_ + 0.0540193266 * mid - 0.113614708 * short,
                    -0.000365296938 * long_ - 0.00412161469 * mid + 0.693511405 * short)

        def assist(rgb):                       # must match colour_assist() in C
            r, g, b = rgb
            return (r, g, max(0.0, min(255.0, b + 0.8 * (g - r))))

        def apart(a, b):
            return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5

        pairs = [((96, 226, 138), (206, 78, 66)),    # rail lamp vs buffer stop
                 ((244, 196, 120), (244, 86, 72)),   # streets signal vs dead end
                 ((82, 214, 190), (255, 64, 76))]    # heatmap healthy vs weak
        for first, second in pairs:
            plain = apart(simulate(first), simulate(second))
            helped = apart(simulate(assist(first)), simulate(assist(second)))
            self.assertGreater(helped, plain * 1.05,
                               "assist must widen %s vs %s" % (first, second))

    def test_colorblind_flag_changes_the_export(self):
        with tempfile.TemporaryDirectory() as tmp:
            shots = []
            for extra in ((), ("--colorblind",)):
                png = Path(tmp) / ("rail%d.png" % len(shots))
                result = self.run_wfc("--mode", "rail", "--seed", "5",
                                      "--w", "16", "--h", "10", "--once",
                                      "--save", os.fspath(png), *extra)
                self.assertEqual(result.returncode, 0, result.stderr)
                shots.append(png.read_bytes())
            self.assertNotEqual(shots[0], shots[1],
                                "--colorblind must reach the export")

    def test_density_flag_is_range_checked(self):
        self.assertEqual(self.run_wfc("--density", "50", "--mode", "streets",
                                      "--w", "8", "--h", "6", "--once").returncode, 0)
        for bad in ("0", "200", "abc"):
            result = self.run_wfc("--density", bad, "--once")
            self.assertEqual(result.returncode, 2, bad)
            self.assertIn("--density", result.stderr)

    def test_coarse_seeded_worlds_solve_at_many_sizes(self):
        """The value-noise lattice must divide the world at every size.

        When it did not, the toroidal wrap landed mid-lattice-cell and the
        seam jumped several bands at once; the +/-2 domain windows stopped
        overlapping there and the grid became unsolvable — but only at some
        sizes, which the fixed-size sweep never hit.
        """
        coarse = ("galaxy", "geode", "stained", "solar", "storm", "glacier", "koi")
        for mode in coarse:
            for w, h in ((6, 5), (12, 8), (19, 11), (40, 20), (60, 30), (97, 41)):
                result = self.run_wfc("--mode", mode, "--seed", "11",
                                      "--w", str(w), "--h", str(h), "--once")
                self.assertEqual(result.returncode, 0,
                                 "%s %dx%d: %s" % (mode, w, h, result.stderr))
                self.assertNotIn("no solution", result.stderr)

    def test_help_lists_every_registered_mode(self):
        """--help renders the mode list from MODES[], so it cannot go stale."""
        modes = self.run_wfc("--list-modes").stdout.split()
        self.assertGreater(len(modes), 20)
        help_text = self.run_wfc("--help").stdout
        self.assertIn("one of %d worlds" % len(modes), help_text)
        for mode in modes:
            self.assertIn(mode, help_text)


if __name__ == "__main__":
    unittest.main()
