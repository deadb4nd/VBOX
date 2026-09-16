#!/usr/bin/env bash
# Flash firmware and website (SPIFFS) to the board.
# Usage: ./flash.sh            # firmware + filesystem
#        ./flash.sh fw         # firmware only
#        ./flash.sh fs         # filesystem only
set -euo pipefail
cd "$(dirname "$0")"

case "${1:-all}" in
  all) pio run -t upload && pio run -t uploadfs ;;
  fw)  pio run -t upload ;;
  fs)  pio run -t uploadfs ;;
  *) echo "usage: $0 [all|fw|fs]"; exit 1 ;;
esac