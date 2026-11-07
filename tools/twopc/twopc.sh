#!/usr/bin/env bash
# Drives a two-PC test session from a dev machine over ssh. See tools/twopc/README.md.
#
#   THEYS_GONE_HOST_SSH=user@host-pc  THEYS_GONE_GUEST_SSH=user@guest-pc  tools/twopc/twopc.sh CMD ...
#
# PC is host, guest or both. Commands:
#   setup PC                 copy remote.sh and vkey.py to the PC
#   status PC                game running?, ASI hash, INI present?, capture backend
#   prep PC LABEL ASI        back up ASI/INI/Saved as ~/TheysGone-backup-LABEL, install ASI, no INI
#   restore PC LABEL         restore that backup and verify its hashes
#   launch PC                start Days Gone through Steam
#   keys PC STEP...          tap keys (see vkey.py: down, enter, esc, sleep:2000, ...)
#   shot PC OUT.png          screenshot the PC's screen into a local file
#   log PC [REGEX [N]]       last N (20) TheysGone.log lines, optionally filtered
#   quit-wait PC [SECONDS]   wait until the game has exited
#
# THEYS_GONE_SSH overrides the ssh command (default: ssh); it must accept the same arguments.
set -euo pipefail
HERE="$(cd -- "$(dirname -- "$0")" && pwd)"
SSH="${THEYS_GONE_SSH:-ssh}"
SCP="${THEYS_GONE_SCP:-scp}"
REMOTE_DIR='.cache/theys-gone-twopc'

die() { echo "twopc: $*" >&2; exit 1; }
target() {
    case "$1" in
    host) echo "${THEYS_GONE_HOST_SSH:?set THEYS_GONE_HOST_SSH=user@host-pc}" ;;
    guest) echo "${THEYS_GONE_GUEST_SSH:?set THEYS_GONE_GUEST_SSH=user@guest-pc}" ;;
    *) die "PC must be host, guest or both, not '$1'" ;;
    esac
}
pcs() { if [ "$1" = both ]; then echo host guest; else target "$1" >/dev/null; echo "$1"; fi; }
quote() { printf '%q ' "$@"; }
remote() { local pc="$1"; shift; $SSH "$(target "$pc")" "bash $REMOTE_DIR/remote.sh $(quote "$@")"; }

[ $# -ge 2 ] || { sed -n '2,19p' "$0"; exit 2; }
cmd="$1"; which="$2"; shift 2
for pc in $(pcs "$which"); do
    [ "$which" = both ] && echo "== $pc"
    case "$cmd" in
    setup)
        $SSH "$(target "$pc")" "mkdir -p $REMOTE_DIR"
        $SCP -q "$HERE/remote.sh" "$HERE/vkey.py" "$(target "$pc"):$REMOTE_DIR/"
        echo "installed the helpers in ~/$REMOTE_DIR"
        ;;
    prep)
        [ $# -eq 2 ] || die "usage: prep PC LABEL ASI"
        $SCP -q "$2" "$(target "$pc"):$REMOTE_DIR/TheysGone.asi"
        remote "$pc" prep "$1" "$REMOTE_DIR/TheysGone.asi"
        ;;
    shot)
        [ $# -eq 1 ] || die "usage: shot PC OUT.png"
        out="$1"; [ "$which" = both ] && out="${1%.png}-$pc.png"
        remote "$pc" shot /tmp/theys-gone-shot.png >/dev/null
        $SCP -q "$(target "$pc"):/tmp/theys-gone-shot.png" "$out"
        echo "$out"
        ;;
    status|restore|launch|keys|log|quit-wait)
        remote "$pc" "$cmd" "$@"
        ;;
    *) die "unknown command '$cmd'" ;;
    esac
done
