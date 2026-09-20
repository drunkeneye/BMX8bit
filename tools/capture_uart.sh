#!/bin/bash

set -euo pipefail

usage() {
  cat <<EOF
Usage: $(basename "$0") SERIAL_DEVICE [LOGFILE] [BAUD]

Example:
  $(basename "$0") /dev/ttyUSB0 uart.log
  $(basename "$0") /dev/ttyUSB0 uart-57600.log 57600
EOF
}

if (($# == 1)) && { [ "$1" = "-h" ] || [ "$1" = "--help" ]; }; then
  usage
  exit 0
fi

if (($# < 1 || $# > 3)); then
  usage
  exit 1
fi

SERIAL_DEVICE="$1"
LOGFILE="${2:-uart-$(date +%Y%m%d-%H%M%S).log}"
BAUD="${3:-115200}"

if [ ! -e "$SERIAL_DEVICE" ]; then
  echo "serial device not found: $SERIAL_DEVICE" >&2
  exit 1
fi

if [ -e "$LOGFILE" ]; then
  echo "refusing to overwrite existing UART log: $LOGFILE" >&2
  echo "choose a new filename so the previous hardware evidence is preserved" >&2
  exit 1
fi

stty -F "$SERIAL_DEVICE" "$BAUD" raw cs8 -cstopb -parenb -ixon -ixoff -crtscts -echo

echo "capturing $SERIAL_DEVICE at $BAUD baud -> $LOGFILE"
echo "start the target only after this line; updater diagnostics use source bmx-update"
echo "press Ctrl-C to stop"

stdbuf -oL cat "$SERIAL_DEVICE" | tee "$LOGFILE"
