#!/usr/bin/env bash
# Run ble_client.py in the project's venv, from any shell and any directory.
# All arguments are passed through, e.g.:
#   ./run.sh --mon
#   ~/proj/go2/go2-fancontrol/run.sh scan --log
set -e
DIR="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
cd "$DIR"
# shellcheck disable=SC1091
source "$DIR/venv/bin/activate"
exec python "$DIR/ble_client.py" --mon "$@"
