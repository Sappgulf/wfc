#!/usr/bin/env python3
"""Test-only audio player: records dispatch without emitting sound."""

import os
import sys

path = os.environ.get("WFC_AUDIO_LOG")
if path:
    with open(path, "a", encoding="utf-8") as handle:
        handle.write((sys.argv[1] if len(sys.argv) > 1 else "missing") + "\n")
