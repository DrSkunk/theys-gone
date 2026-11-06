#!/usr/bin/env bash
# Runs on a Linux/Proton test PC; twopc.sh copies it there and calls it over ssh.
# Commands: prep LABEL ASI | restore LABEL | launch | quit-wait [SECONDS] | keys STEP... |
#           shot OUT.png | log [REGEX] | status
set -euo pipefail
APP=1259420
STEAM_ROOT="${THEYS_GONE_STEAM_ROOT:-$HOME/.local/share/Steam}"
GAME="$STEAM_ROOT/steamapps/common/Days Gone/BendGame/Binaries/Win64"
SAVED="$STEAM_ROOT/steamapps/compatdata/$APP/pfx/drive_c/users/steamuser/AppData/Local/BendGame/Saved"
HERE="$(cd -- "$(dirname -- "$0")" && pwd)"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
export DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-unix:path=$XDG_RUNTIME_DIR/bus}"

# Only the real install counts: headless lab clones (tools/lab) run their own DaysGone.exe elsewhere.
running() { pgrep -f '[c]ommon/Days Gone/BendGame/Binaries/Win64/DaysGone.exe' >/dev/null; }
die() { echo "remote: $*" >&2; exit 1; }
backup_dir() {
    case "$1" in ''|*[!A-Za-z0-9._-]*) die "label must be letters, digits, '.', '_' or '-'" ;; esac
    echo "$HOME/TheysGone-backup-$1"
}
hash_saves() { (cd "$SAVED" && find . -type f -print0 | sort -z | xargs -0 sha256sum); }

# Desktop sessions (KDE Plasma) capture with spectacle; Steam Deck game mode with gamescopectl.
capture_backend() {
    if [ -n "${THEYS_GONE_CAPTURE:-}" ]; then echo "$THEYS_GONE_CAPTURE"
    elif pgrep -x kwin_wayland >/dev/null; then echo spectacle
    else echo gamescope; fi
}
desktop_wayland() {
    local socket
    for socket in "$XDG_RUNTIME_DIR"/wayland-*; do
        case "$socket" in *.lock) continue ;; esac
        [ -S "$socket" ] && { basename "$socket"; return; }
    done
    die "no desktop Wayland socket in $XDG_RUNTIME_DIR"
}

cmd="${1:-}"; shift || true
case "$cmd" in
prep)
    # Back up ASI, INI and the whole Saved tree with hashes, then install the ASI with no INI.
    [ $# -eq 2 ] || die "usage: prep LABEL ASI"
    B="$(backup_dir "$1")"; asi="$2"
    running && die "the game is running"
    [ -e "$B" ] && die "$B already exists"
    [ -f "$asi" ] || die "no ASI at $asi"
    mkdir -p "$B"
    cp -a "$GAME/TheysGone.asi" "$B/"
    [ -f "$GAME/TheysGone.ini" ] && cp -a "$GAME/TheysGone.ini" "$B/"
    [ -f "$GAME/TheysGone.log" ] && mv "$GAME/TheysGone.log" "$B/TheysGone.log.before"
    tar -C "$(dirname "$SAVED")" -cf "$B/Saved.tar" Saved
    hash_saves > "$B/saved.sha256"
    files=(TheysGone.asi); [ -f "$B/TheysGone.ini" ] && files+=(TheysGone.ini)
    (cd "$B" && sha256sum "${files[@]}") > "$B/files.sha256"
    cp "$asi" "$GAME/.TheysGone.asi.new" && mv "$GAME/.TheysGone.asi.new" "$GAME/TheysGone.asi"
    rm -f "$GAME/TheysGone.ini"
    echo "backed up $(wc -l < "$B/saved.sha256") save files to $B"
    echo "installed $(sha256sum "$GAME/TheysGone.asi" | cut -c1-16) with no INI"
    ;;
restore)
    # Keep the post-test state, then put ASI, INI and Saved back and compare hashes.
    [ $# -eq 1 ] || die "usage: restore LABEL"
    B="$(backup_dir "$1")"
    [ -f "$B/Saved.tar" ] || die "no backup at $B"
    running && die "the game is running"
    tar -tf "$B/Saved.tar" >/dev/null
    mkdir -p "$B/after"
    [ -f "$GAME/TheysGone.log" ] && cp -a "$GAME/TheysGone.log" "$B/after/TheysGone.log"
    tar -C "$(dirname "$SAVED")" -cf "$B/after/Saved.tar" Saved
    cp "$B/TheysGone.asi" "$GAME/.TheysGone.asi.new" && mv "$GAME/.TheysGone.asi.new" "$GAME/TheysGone.asi"
    if [ -f "$B/TheysGone.ini" ]; then cp -a "$B/TheysGone.ini" "$GAME/"; else rm -f "$GAME/TheysGone.ini"; fi
    if [ -f "$B/TheysGone.log.before" ]; then cp -a "$B/TheysGone.log.before" "$GAME/TheysGone.log"; fi
    rm -rf "$SAVED"
    tar -C "$(dirname "$SAVED")" -xf "$B/Saved.tar"
    hash_saves > "$B/saved-restored.sha256"
    cmp -s "$B/saved.sha256" "$B/saved-restored.sha256" || die "SAVE HASHES DIFFER after restore"
    (cd "$GAME" && sha256sum -c --quiet "$B/files.sha256") || die "ASI/INI HASHES DIFFER after restore"
    echo "restored: $(wc -l < "$B/saved.sha256") save files, ASI and INI match the backup"
    ;;
launch)
    running && { echo "already running"; exit 0; }
    export DISPLAY="${DISPLAY:-:0}"
    if [ "$(capture_backend)" = spectacle ]; then
        WAYLAND_DISPLAY="$(desktop_wayland)"; export WAYLAND_DISPLAY
        timeout 20 steam -ifrunning "steam://rungameid/$APP" >/dev/null 2>&1 || true
    else
        nohup steam "steam://rungameid/$APP" >/dev/null 2>&1 &
    fi
    for _ in $(seq 1 60); do running && { echo "started"; exit 0; }; sleep 1; done
    die "the game did not start within 60 s"
    ;;
quit-wait)
    seconds="${1:-60}"
    for _ in $(seq 1 "$seconds"); do running || { echo "exited"; exit 0; }; sleep 1; done
    die "still running after $seconds s"
    ;;
keys)
    python3 "$HERE/vkey.py" "$@"
    ;;
shot)
    [ $# -eq 1 ] || die "usage: shot OUT.png"
    rm -f "$1"
    if [ "$(capture_backend)" = spectacle ]; then
        WAYLAND_DISPLAY="$(desktop_wayland)" timeout 20 spectacle -b -n -f -o "$1" >/dev/null 2>&1 || true
    else
        gamescopectl screenshot "$1" >/dev/null 2>&1 || true
        sleep 2  # gamescope writes the file asynchronously
    fi
    [ -s "$1" ] || die "no screenshot written"
    echo "$1"
    ;;
log)
    [ -f "$GAME/TheysGone.log" ] || die "no TheysGone.log"
    if [ $# -ge 1 ]; then grep -E "$1" "$GAME/TheysGone.log" | tail -n "${2:-20}" || true
    else tail -n 20 "$GAME/TheysGone.log"; fi
    ;;
status)
    running && echo "game: running" || echo "game: not running"
    [ -f "$GAME/TheysGone.asi" ] && echo "asi: $(sha256sum "$GAME/TheysGone.asi" | cut -c1-16)"
    [ -f "$GAME/TheysGone.ini" ] && echo "ini: present" || echo "ini: none"
    [ -f "$GAME/TheysGone.log" ] && head -n 1 "$GAME/TheysGone.log"
    echo "capture: $(capture_backend)"
    ;;
*)
    die "unknown command '$cmd'"
    ;;
esac
