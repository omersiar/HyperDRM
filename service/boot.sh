#!/bin/sh
# HyperDRM: container-owned startup script, linked by the app.
SERVICE_DIR=/media/developer/apps/usr/palm/services/org.webosbrew.histogramhyperion.service
TARGET="$SERVICE_DIR/histogram-service"
HOOK=/var/lib/webosbrew/init.d/60-histogramhyperion
[ -x "$TARGET" ] && [ -f "$SERVICE_DIR/services.json" ] || exit 0
command -v luna-send >/dev/null 2>&1 || exit 0
command -v timeout >/dev/null 2>&1 || exit 0
(
  echo 'HyperDRM boot start requested'
  attempt=0
  while [ -x "$TARGET" ] && [ -f "$SERVICE_DIR/services.json" ] && [ -L "$HOOK" ] && [ "$(readlink "$HOOK")" = "$SERVICE_DIR/boot.sh" ] && [ "$attempt" -lt 120 ]; do
    result=$(timeout 12 luna-send -n 1 luna://org.webosbrew.histogramhyperion.service/start '{}' 2>&1)
    case "$result" in
      *'"running":true'*) echo 'HyperDRM service started'; exit 0 ;;
    esac
    attempt=$((attempt + 1))
    printf 'Attempt %s failed: %.512s\n' "$attempt" "$result"
    sleep 5
  done
  echo 'HyperDRM boot start cancelled, target removed or retry limit reached'
) >/var/lib/webosbrew/histogramhyperion-boot.log 2>&1 </dev/null &
exit 0
