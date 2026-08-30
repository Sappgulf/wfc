"""A HOME that is not the user's.

wfc remembers mode, theme, speed, density, audio and CRT in ~/.wfcrc, and
writes it on exit. Every test that launched the binary was therefore reading
and rewriting the developer's own settings — and a test that turned sound on
left it on, which made later runs spawn an audio player and hang the suite.
Tests point HOME at a scratch directory so a run cannot reach outside itself.
"""

import os
import tempfile

_sandbox = None


def home():
    """Path to the shared per-session scratch HOME, created on first use."""
    global _sandbox
    if _sandbox is None:
        _sandbox = tempfile.mkdtemp(prefix="wfc-test-home-")
    return _sandbox


def env(**overrides):
    """A copy of the environment with HOME redirected, plus any overrides."""
    out = os.environ.copy()
    out["HOME"] = home()
    out.pop("XDG_CONFIG_HOME", None)
    out.update(overrides)
    return out
