#!/usr/bin/env python3
"""Host-boundary smoke tests for inline graphics and audio dispatch."""

import argparse
import fcntl
import os
import pty
import select
import signal
import struct
import subprocess
import sys
import termios
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def stop_child(pid):
    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    deadline = time.time() + 1
    while time.time() < deadline:
        try:
            waited, _ = os.waitpid(pid, os.WNOHANG)
        except ChildProcessError:
            return
        if waited == pid:
            return
        time.sleep(0.02)
    try:
        os.kill(pid, signal.SIGKILL)
    except ProcessLookupError:
        return
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass


def graphics(binary):
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(ROOT)
        env = os.environ.copy()
        env["HOME"] = tempfile.mkdtemp(prefix="wfc-gfx-home-")
        env["TERM_PROGRAM"] = "iTerm.app"
        os.execve(str(binary), [str(binary), "--mode", "aurora", "--seed", "7",
                                "--w", "8", "--h", "6", "--once", "--gfx"], env)
    fcntl_size = struct.pack("HHHH", 30, 100, 0, 0)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, fcntl_size)
    output = b""
    deadline = time.time() + 8
    try:
        while time.time() < deadline:
            ready, _, _ = select.select([fd], [], [], 0.1)
            if ready:
                try:
                    output += os.read(fd, 65536)
                except OSError:
                    break
            if b"\x1b]1337;File=inline=1;" in output:
                break
    finally:
        stop_child(pid)
        os.close(fd)
    if b"\x1b]1337;File=inline=1;" not in output:
        raise AssertionError("graphics protocol frame was not emitted")


def audio(binary):
    with tempfile.TemporaryDirectory(prefix="wfc-audio-") as temp:
        log = Path(temp) / "audio.log"
        env = os.environ.copy()
        env["HOME"] = temp
        env["WFC_AUDIO_PLAYER"] = str(ROOT / "tests" / "fake_audio.py")
        env["WFC_AUDIO_LOG"] = str(log)
        result = subprocess.run(
            [str(binary), "--mode", "fire", "--seed", "7", "--w", "8", "--h", "6",
             "--once", "--sound"],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=15,
        )
        if result.returncode != 0:
            raise AssertionError(result.stderr)
        deadline = time.time() + 2
        while time.time() < deadline and not log.exists():
            time.sleep(0.02)
        if not log.exists() or not log.read_text(encoding="utf-8").strip():
            raise AssertionError("audio player override was not invoked")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default=str(ROOT / "wfc"))
    parser.add_argument("--graphics", action="store_true")
    parser.add_argument("--audio", action="store_true")
    args = parser.parse_args()
    binary = Path(args.binary).resolve()
    if args.graphics:
        graphics(binary)
        print("graphics: inline image protocol OK")
    if args.audio:
        audio(binary)
        print("audio: dispatch and WAV pipeline OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
