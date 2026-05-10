#!/usr/bin/env bash
set -euo pipefail

VENV="${STEMSEP_VENV:-$HOME/.venvs/stemsep}"
REAPER="${REAPER:-$HOME/reaper_linux_x86_64/REAPER/reaper}"

if [[ ! -d "$VENV" ]]; then
    echo "Venv not found: $VENV" >&2
    echo "Create one with:" >&2
    echo "  python3 -m venv $VENV && $VENV/bin/pip install demucs soundfile julius" >&2
    exit 1
fi

if [[ ! -x "$REAPER" ]]; then
    echo "Reaper binary not found: $REAPER" >&2
    echo "Set REAPER env var, e.g. REAPER=/path/to/reaper $0" >&2
    exit 1
fi

export PATH="$VENV/bin:$PATH"

if ! python3 -c "import demucs, soundfile" 2>/dev/null; then
    echo "demucs/soundfile not importable from $VENV." >&2
    echo "Install with: $VENV/bin/pip install demucs soundfile julius" >&2
    exit 1
fi

echo ">>> Venv:   $VENV"
echo ">>> Reaper: $REAPER"
exec "$REAPER" "$@"
