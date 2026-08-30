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
BINARY = ROOT / "wfc"
ESC = b"\x1b"


class Session:
    """A wfc process on a pty of a known size."""

    def __init__(self, *args, cols=120, rows=40):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:                       # child
            os.chdir(ROOT)
            os.execve(str(BINARY), [str(BINARY), "--mode", "circuit",
                                    "--w", "40", "--h", "20", *args],
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
        opened = self.session.send(b"/")
        self.assertIn("PICK A WORLD", plain(opened))
        self.assertEqual(preview_label(opened), "circuit",
                         "picker opens on the current world")

        moved = self.session.send(ESC + b"[B")
        self.assertEqual(preview_label(moved), "terrain",
                         "down arrow must move the selection")

        typed = self.session.send(b"rail")
        text = plain(typed)
        self.assertIn("search: rail", text)
        self.assertIn("1/33 worlds", text)
        self.assertEqual(preview_label(typed), "rail")

    def test_picker_selects_on_enter_and_cancels_on_escape(self):
        self.session = Session()
        self.session.send(b"/")
        self.session.send(b"vin")
        chosen = self.session.send(b"\r", wait=1.6)
        self.assertNotIn("PICK A WORLD", last_screen(chosen),
                         "enter must close the picker")
        self.assertIn("vinyl", plain(chosen), "enter switches to the world")

        reopened = self.session.send(b"/")
        self.assertIn("PICK A WORLD", plain(reopened))
        cancelled = self.session.send(ESC, wait=1.4)
        self.assertNotIn("PICK A WORLD", last_screen(cancelled),
                         "escape must close the picker")

    def test_arrow_keys_do_not_swallow_the_keys_that_follow(self):
        """ESC used to strand the reader in the mouse state machine."""
        self.session = Session()
        self.session.send(ESC + b"[A" + ESC + b"[D", wait=0.8)
        after = self.session.send(b"/")
        self.assertIn("PICK A WORLD", plain(after),
                      "a key after an arrow must still register")

    def test_q_quits(self):
        self.session = Session()
        self.session.send(b"q", wait=1.5)
        self.assertFalse(self.session.alive(), "q must exit")


if __name__ == "__main__":
    unittest.main()
