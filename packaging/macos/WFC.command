#!/bin/sh
# Double-clickable macOS launcher. It works from the source checkout and from
# Contents/Resources inside WFC.app, forwarding replay links and dropped files.
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
if [ -x "$script_dir/../MacOS/wfc" ]; then
    executable="$script_dir/../MacOS/wfc"
else
    executable="$script_dir/../../wfc"
fi

if [ ! -x "$executable" ]; then
    echo "WFC executable not found: $executable" >&2
    exit 1
fi

exec "$executable" "$@"
