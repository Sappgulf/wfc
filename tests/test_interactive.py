"""Drive the live TUI through a pty.

Everything else in the suite runs headless, so the interactive surface — the
key handling, the overlays, the escape-sequence reader — had no coverage at
all. That is where arrow keys were silently jamming the input parser.
"""

import fcntl
import os
import pty
import re
import select
import signal
import struct
import sys
import termios
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sandbox  # noqa: E402


ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get("WFC_BINARY", ROOT / "wfc"))
ESC = b"\x1b"


class Session:
    """A wfc process on a pty of a known size."""

    def __init__(self, *args, cols=120, rows=40):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:                       # child
            os.chdir(ROOT)
            size = ["--w", "6", "--h", "5"] if "--infinite" in args else \
                   ["--w", "40", "--h", "20"]
            os.execve(str(BINARY), [str(BINARY), "--mode", "circuit",
                                    *size, *args],
                      sandbox.env())
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ,
                    struct.pack("HHHH", rows, cols, 0, 0))
        self.settle(1.2)

    def drain(self, seconds=0.4):
        out = b""
        end = time.time() + seconds
        while time.time() < end:
            ready, _, _ = select.select([self.fd], [], [], 0.05)
            if ready:
                try:
                    out += os.read(self.fd, 65536)
                except OSError:
                    break
        return out

    def settle(self, seconds=1.0):
        self.drain(seconds)

    def send(self, keys, wait=1.2):
        """Send keys and return only what was drawn afterwards."""
        self.drain(0.05)
        os.write(self.fd, keys)
        time.sleep(wait)
        return self.drain(0.4)

    def send_until(self, keys, predicate, timeout=12.0):
        """Send keys, then read until the screen satisfies `predicate`.

        Fixed sleeps made this suite pass alone and fail inside `make check`,
        where the sanitiser sweep and benchmarks are competing for the machine
        — the app simply had not redrawn yet. Waiting on the screen instead of
        on the clock is both reliable under load and quicker when idle.
        """
        self.drain(0.05)
        os.write(self.fd, keys)
        seen = b""
        deadline = time.time() + timeout
        while time.time() < deadline:
            seen += self.drain(0.3)
            if predicate(seen):
                return seen + self.drain(0.2)
        return seen

    def alive(self):
        try:
            return os.waitpid(self.pid, os.WNOHANG) == (0, 0)
        except ChildProcessError:
            return False

    def close(self):
        try:
            os.kill(self.pid, signal.SIGKILL)
            os.waitpid(self.pid, 0)
        except (ProcessLookupError, ChildProcessError):
            pass
        try:
            os.close(self.fd)
        except OSError:
            pass


def plain(raw):
    return re.sub(rb"\x1b\[[0-9;?]*[A-Za-z]", b"", raw).decode("utf-8", "replace")


def last_screen(raw):
    """Only the final frame.

    The app redraws every 50ms, so by the time a keystroke takes effect the
    pty already holds a backlog of frames drawn before it. Asserting over the
    whole buffer tests the past as well as the present; split on the clear and
    keep the last screen.
    """
    frames = raw.split(b"\x1b[2J")
    return plain(frames[-1] if frames else raw)


def preview_label(raw):
    """The world name printed under the picker's thumbnail."""
    found = re.findall(r"▀([a-z]+)", plain(raw))
    return found[-1] if found else None


class InteractiveTests(unittest.TestCase):
    def setUp(self):
        self.session = None

    def tearDown(self):
        if self.session:
            self.session.close()

    def test_picker_filters_and_previews_the_selected_world(self):
        self.session = Session()
        opened = self.session.send_until(b"/", lambda o: preview_label(o) == "circuit")
        self.assertIn("PICK A WORLD", plain(opened))
        self.assertEqual(preview_label(opened), "circuit",
                         "picker opens on the current world")

        moved = self.session.send_until(ESC + b"[B",
                                        lambda o: preview_label(o) == "terrain")
        self.assertEqual(preview_label(moved), "terrain",
                         "down arrow must move the selection")

        typed = self.session.send_until(b"rail", lambda o: "search: rail" in plain(o))
        text = plain(typed)
        self.assertIn("search: rail", text)
        self.assertIn("1/37 worlds", text)
        self.assertEqual(preview_label(typed), "rail")

    def test_picker_filters_by_mode_tag(self):
        self.session = Session()
        self.session.send_until(b"/", lambda o: "PICK A WORLD" in plain(o))
        filtered = self.session.send_until(
            b"#network", lambda o: "search: #network" in plain(o) and "rail" in plain(o),
        )
        text = last_screen(filtered)
        self.assertIn("streets", text)
        self.assertIn("rail", text)
        self.assertNotIn("tide", text)
        self.assertIn("#field", text)
        self.assertIn("#animated", text)

    def test_help_explains_the_first_three_interactions(self):
        self.session = Session()
        opened = self.session.send_until(
            b"h", lambda o: "QUICK START" in plain(o), timeout=8,
        )
        text = plain(opened)
        self.assertIn("QUICK START", text)
        self.assertIn("1  pick a world", text)
        self.assertIn("2  wait for collapse", text)
        self.assertIn("press any key", text.lower())

    def test_picker_selects_on_enter_and_cancels_on_escape(self):
        self.session = Session()
        self.session.send_until(b"/", lambda o: "PICK A WORLD" in plain(o))
        self.session.send_until(b"vin", lambda o: "search: vin" in plain(o))
        chosen = self.session.send_until(
            b"\r", lambda o: "PICK A WORLD" not in last_screen(o))
        self.assertNotIn("PICK A WORLD", last_screen(chosen),
                         "enter must close the picker")
        self.assertIn("vinyl", plain(chosen), "enter switches to the world")

        reopened = self.session.send_until(b"/", lambda o: "PICK A WORLD" in plain(o))
        self.assertIn("PICK A WORLD", plain(reopened))
        cancelled = self.session.send_until(
            ESC, lambda o: "PICK A WORLD" not in last_screen(o))
        self.assertNotIn("PICK A WORLD", last_screen(cancelled),
                         "escape must close the picker")

    def test_arrow_keys_do_not_swallow_the_keys_that_follow(self):
        """ESC used to strand the reader in the mouse state machine."""
        self.session = Session()
        self.session.send(ESC + b"[A" + ESC + b"[D", wait=0.8)
        after = self.session.send_until(b"/", lambda o: "PICK A WORLD" in plain(o))
        self.assertIn("PICK A WORLD", plain(after),
                      "a key after an arrow must still register")

    def test_thermo_survives_an_infinite_world_growing(self):
        """A relaunched worker must not inherit the dead one's half-line.

        thermo_kill() left the reader's partial-line buffer intact, so the
        next worker's first bytes were appended to that stale prefix and
        parsed as one spliced frame. After --infinite grew the world that
        produced a config of the wrong length and failed the solver outright.
        Every relaunch hits the same path — toggling T, --reset-learning.
        """
        self.session = Session("--solver", "thermo", "--infinite",
                               "--speed", "20000")
        seen = b""
        deadline = time.time() + 45
        while time.time() < deadline and "grew to" not in plain(seen):
            seen += self.session.drain(1.0)
        text = plain(seen)
        self.assertIn("grew to", text, "the world never grew; test proved nothing")
        self.assertNotIn("thermo failed", text)

    def test_q_quits_while_a_finished_world_is_lingering(self):
        """Keys were dropped for the whole post-solve linger.

        That loop pumped the keyboard but discarded what it returned, so q,
        space and m did nothing at all while a finished world sat on screen —
        up to 4.5 seconds of the app appearing to ignore you. At a high speed
        the solve lands almost immediately, so this presses q right into it.
        """
        self.session = Session("--speed", "20000")
        self.session.drain(0.4)
        os.write(self.session.fd, b"q")
        deadline = time.time() + 8
        while time.time() < deadline and self.session.alive():
            self.session.drain(0.15)
        self.assertFalse(self.session.alive(), "q must exit during the linger")

    def test_q_quits(self):
        self.session = Session()
        self.session.drain(0.2)
        os.write(self.session.fd, b"q")
        # keep reading while waiting: a full pty buffer blocks the app's
        # writes, and an app blocked on write never gets back to the keys
        deadline = time.time() + 10
        while time.time() < deadline and self.session.alive():
            self.session.drain(0.2)
        self.assertFalse(self.session.alive(), "q must exit")


if __name__ == "__main__":
    unittest.main()
