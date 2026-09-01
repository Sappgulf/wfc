#!/usr/bin/env python3
"""Report the executable, optional thermo stack, and host rendering/audio paths."""

import argparse
import importlib.util
import json
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path


def probe_modules(interpreter):
    code = (
        "import importlib.util,json; "
        "names=['numpy','jax','thrml']; "
        "print(json.dumps({n: bool(importlib.util.find_spec(n)) for n in names}))"
    )
    try:
        result = subprocess.run(
            [interpreter, "-c", code], capture_output=True, text=True, timeout=5
        )
        return json.loads(result.stdout) if result.returncode == 0 else {}
    except (OSError, subprocess.SubprocessError, json.JSONDecodeError):
        return {}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default="./wfc")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    binary = Path(args.binary).resolve()
    root = binary.parent
    local_thermo_python = root / ".venv-thermo" / "bin" / "python"
    interpreter = (os.environ.get("WFC_PYTHON") or
                   (str(local_thermo_python) if local_thermo_python.exists() else sys.executable))
    interpreter_path = shutil.which(interpreter) or interpreter
    thermo_script = Path(os.environ.get("WFC_THERMO_PY", root / "wfc_thermo.py"))
    if not thermo_script.exists():
        thermo_script = root / "wfc_thermo.py"

    version = "unavailable"
    modes = []
    if binary.exists():
        try:
            version = subprocess.run(
                [str(binary), "--version"], capture_output=True, text=True,
                timeout=5, check=True
            ).stdout.strip()
            modes = subprocess.run(
                [str(binary), "--list-modes"], capture_output=True, text=True,
                timeout=5, check=True
            ).stdout.splitlines()
        except (OSError, subprocess.SubprocessError):
            pass

    modules = probe_modules(interpreter_path)
    has_thrml_stack = all(modules.get(name, False) for name in ("numpy", "jax", "thrml"))
    data = {
        "binary": str(binary),
        "version": version,
        "modes": len(modes),
        "platform": platform.platform(),
        "python": interpreter_path,
        "thermo_script": str(thermo_script),
        "thermo_script_readable": os.access(thermo_script, os.R_OK),
        "optional_modules": modules,
        "backends": {
            "classic": True,
            "python-bounded": bool(thermo_script.exists()),
            "thrml-jax-available": has_thrml_stack,
        },
        "graphics": {
            "term_program": os.environ.get("TERM_PROGRAM", ""),
            "kitty": bool(os.environ.get("KITTY_WINDOW_ID") or "kitty" in os.environ.get("TERM", "")),
            "ghostty": os.environ.get("TERM_PROGRAM") == "ghostty",
        },
        "audio_players": {
            name: bool(shutil.which(name)) for name in ("afplay", "aplay")
        },
    }
    if args.json:
        print(json.dumps(data, sort_keys=True))
    else:
        print("wfc doctor")
        print("  executable : %s (%s)" % (data["binary"], data["version"]))
        print("  registry   : %d modes" % data["modes"])
        print("  platform   : %s" % data["platform"])
        print("  python     : %s" % data["python"])
        print("  thermo     : script=%s bounded=%s thrml+jax=%s" % (
            data["thermo_script"],
            "ready" if data["backends"]["python-bounded"] else "missing",
            "available" if data["backends"]["thrml-jax-available"] else "not installed",
        ))
        print("  graphics   : TERM_PROGRAM=%s kitty=%s ghostty=%s" % (
            data["graphics"]["term_program"] or "unset",
            "yes" if data["graphics"]["kitty"] else "no",
            "yes" if data["graphics"]["ghostty"] else "no",
        ))
        print("  audio      : afplay=%s aplay=%s" % (
            "yes" if data["audio_players"]["afplay"] else "no",
            "yes" if data["audio_players"]["aplay"] else "no",
        ))
    return 0 if binary.exists() and version != "unavailable" else 1


if __name__ == "__main__":
    raise SystemExit(main())
