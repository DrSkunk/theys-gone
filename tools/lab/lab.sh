#!/usr/bin/env bash
# Headless test lab for Days Gone on one Linux/Proton PC.
#
# Each instance is a disposable btrfs/XFS reflink clone of an isolated game copy and its Proton prefix,
# so tests never write to the base copy or to a player's install and saves. Every instance runs in
# its own headless gamescope, which gives it a private X display for input and a gamescope socket
# for screenshots; nothing appears on the desktop. An instance can also run inside a bubblewrap
# sandbox with its own Steam client (a second Steam account), so two games can run side by side.
#
# Usage: lab.sh <command> [args]   (run lab.sh help)
set -euo pipefail

LAB="${THEYS_GONE_LAB:-$HOME/TheysGone-lab}"
BASE="${THEYS_GONE_LAB_BASE:-$HOME/TheysGone-coop-test/run1}"
STEAM_ROOT="${THEYS_GONE_STEAM:-$HOME/.local/share/Steam}"
PROTON="${THEYS_GONE_PROTON:-$STEAM_ROOT/steamapps/common/Proton - Experimental/proton}"
RUNTIME="${THEYS_GONE_RUNTIME:-$STEAM_ROOT/steamapps/common/SteamLinuxRuntime_4/_v2-entry-point}"
APP_ID=1259420
WIDTH="${THEYS_GONE_LAB_WIDTH:-1280}"
HEIGHT="${THEYS_GONE_LAB_HEIGHT:-720}"
STEAM_WAIT="${THEYS_GONE_LAB_STEAM_WAIT:-60}"
here="$(cd "$(dirname "$0")" && pwd)"

die() { echo "lab: $*" >&2; exit 1; }
need() { command -v "$1" >/dev/null || die "$1 is required"; }

valid_name() { [[ "$1" =~ ^[a-z][a-z0-9-]{0,31}$ ]] || die "instance names are lowercase letters, digits and dashes"; }
dir_of() { valid_name "$1"; echo "$LAB/$1"; }
win64() { echo "$(dir_of "$1")/game/BendGame/Binaries/Win64"; }
unit_of() { echo "theys-gone-lab-$1"; }
running() { systemctl --user is-active --quiet "$(unit_of "$1")"; }

# The gamescope Wayland socket and X display of a running instance, from its gamescope log.
gamescope_socket() {
    sed -n "s/.*Running compositor on wayland display '\([^']*\)'.*/\1/p" "$(dir_of "$1")/gamescope.log" | tail -1
}
x_display() {
    sed -n 's/.*Starting Xwayland on \(:[0-9]*\).*/\1/p' "$(dir_of "$1")/gamescope.log" | tail -1
}

cmd_create() {
    local name="${1:?instance name}" dir
    dir="$(dir_of "$name")"
    [ -e "$dir" ] && die "$dir already exists (lab.sh destroy $name first)"
    [ -f "$BASE/game/BendGame/Binaries/Win64/DaysGone.exe" ] || die "no isolated game copy at $BASE/game"
    [ -d "$BASE/prefix/pfx" ] || die "no isolated Proton prefix at $BASE/prefix"
    mkdir -p "$dir"
    # A reflink clone shares blocks with the base until either side writes; it fails rather than
    # silently making a 60 GB full copy on filesystems without reflinks.
    cp -a --reflink=always "$BASE/game" "$dir/game"
    cp -a --reflink=always "$BASE/prefix" "$dir/prefix"
    rm -f "$(win64 "$name")"/TheysGone*.txt "$(win64 "$name")/TheysGone.log"
    echo "created $dir from $BASE"
}

cmd_destroy() {
    local name="${1:?instance name}" dir
    dir="$(dir_of "$name")"
    running "$name" && die "$name is running (lab.sh stop $name first)"
    [ -d "$dir" ] || die "no instance $name"
    [[ "$dir" == "$LAB"/* ]] || die "refusing to remove $dir"
    rm -rf --one-file-system "$dir"
    echo "destroyed $name"
}

# Sets [Coop] (or another section with Section.Key=value) INI keys of an instance.
cmd_config() {
    local name="${1:?instance name}" ini
    shift
    ini="$(win64 "$name")/TheysGone.ini"
    [ -f "$ini" ] || die "no TheysGone.ini in $name"
    for pair in "$@"; do
        local key="${pair%%=*}" value="${pair#*=}" section=Coop
        [[ "$pair" == *=* ]] || die "expected Key=value, got $pair"
        if [[ "$key" == *.* ]]; then section="${key%%.*}"; key="${key#*.}"; fi
        [[ "$section$key" =~ ^[A-Za-z0-9]+$ ]] || die "invalid key $pair"
        python3 - "$ini" "$section" "$key" "$value" <<'EOF'
import sys
path, section, key, value = sys.argv[1:]
lines = open(path, encoding="utf-8").read().splitlines()
out, current, done = [], None, False
for line in lines:
    stripped = line.strip()
    if stripped.startswith("[") and stripped.endswith("]"):
        if current == section and not done:
            out.append(f"{key}={value}")
            done = True
        current = stripped[1:-1]
    elif current == section and stripped.split("=", 1)[0].strip() == key and not done:
        line, done = f"{key}={value}", True
    out.append(line)
if not done:
    if current != section:
        out.append(f"[{section}]")
    out.append(f"{key}={value}")
open(path, "w", encoding="utf-8").write("\n".join(out) + "\n")
EOF
    done
    local keys=()
    for pair in "$@"; do keys+=("${pair%%=*}"); keys[-1]="${keys[-1]#*.}"; done
    grep -E "^($(IFS='|'; echo "${keys[*]}"))=" "$ini" || true
}

cmd_deploy() {
    local name="${1:?instance name}" asi="${2:?path to TheysGone.asi}"
    [ -f "$asi" ] || die "no file $asi"
    install -m 0644 "$asi" "$(win64 "$name")/TheysGone.asi.new"
    mv -f "$(win64 "$name")/TheysGone.asi.new" "$(win64 "$name")/TheysGone.asi"
    sha256sum "$(win64 "$name")/TheysGone.asi"
}

# start <name> [--steam-home DIR]
cmd_start() {
    local name="${1:?instance name}" dir steam_home="" inner
    shift
    while [ $# -gt 0 ]; do
        case "$1" in
            --steam-home) steam_home="${2:?}"; shift 2 ;;
            *) die "unknown option $1" ;;
        esac
    done
    need gamescope
    dir="$(dir_of "$name")"
    [ -d "$dir" ] || die "no instance $name"
    running "$name" && die "$name is already running"
    : > "$dir/gamescope.log"
    local launch=(env STEAM_COMPAT_DATA_PATH="$dir/prefix" STEAM_COMPAT_APP_ID=$APP_ID SteamAppId=$APP_ID
                  SteamGameId=$APP_ID WINEDLLOVERRIDES=version=native,builtin)
    local game=("$RUNTIME" --verb=waitforexitandrun -- "$PROTON" run "$(win64 "$name")/DaysGone.exe")
    local scope=(gamescope --backend headless -w "$WIDTH" -h "$HEIGHT" -W "$WIDTH" -H "$HEIGHT" --)
    if [ -z "$steam_home" ]; then
        # Uses the desktop's own Steam client and account.
        inner=("${launch[@]}" STEAM_COMPAT_CLIENT_INSTALL_PATH="$STEAM_ROOT" "${game[@]}")
        systemd-run --user --unit="$(unit_of "$name")" --collect --working-directory="$(win64 "$name")" \
            -p StandardOutput="truncate:$dir/gamescope.log" -p StandardError="append:$dir/gamescope.log" \
            "${scope[@]}" "${inner[@]}" >/dev/null
    else
        need bwrap
        [ -x "$steam_home/.local/share/Steam/steam.sh" ] || die "no Steam client in $steam_home (run lab.sh steam-login $steam_home)"
        # One sandbox holds this instance's Steam client and game: a private /tmp, /dev/shm and SysV IPC
        # keep Steam's single-instance and client IPC apart from the desktop's. bwrap's user namespace
        # shows the host's root-owned /tmp/.X11-unix as foreign, which gamescope rejects, so the sandbox
        # gets its own; Xwayland's abstract socket keeps the display reachable from outside.
        rm -rf "$dir/tmp"
        mkdir -p "$dir/tmp/.X11-unix"
        chmod 1777 "$dir/tmp" "$dir/tmp/.X11-unix"
        local sandbox=(bwrap --dev-bind / / --bind "$dir/tmp" /tmp --tmpfs /dev/shm
                       --unshare-ipc --die-with-parent --setenv HOME "$steam_home")
        local script
        script="$(printf '%q ' "$steam_home/.local/share/Steam/steam.sh" -silent -nofriendsui -no-browser) >\"\$LAB_DIR/steam.log\" 2>&1 & sleep $STEAM_WAIT; exec $(printf '%q ' "${launch[@]}" STEAM_COMPAT_CLIENT_INSTALL_PATH="$steam_home/.local/share/Steam" "${game[@]}")"
        systemd-run --user --unit="$(unit_of "$name")" --collect --working-directory="$(win64 "$name")" \
            --setenv=LAB_DIR="$dir" \
            -p StandardOutput="truncate:$dir/gamescope.log" -p StandardError="append:$dir/gamescope.log" \
            "${sandbox[@]}" -- "${scope[@]}" bash -c "$script" >/dev/null
    fi
    for _ in $(seq 1 60); do
        [ -n "$(x_display "$name")" ] && [ -n "$(gamescope_socket "$name")" ] && break
        sleep 1
    done
    echo "started $name: display $(x_display "$name"), gamescope socket $(gamescope_socket "$name")"
}

cmd_stop() {
    local name="${1:?instance name}"
    running "$name" || { echo "$name is not running"; return 0; }
    systemctl --user stop "$(unit_of "$name")"
    for _ in $(seq 1 30); do
        pgrep -f "$(win64 "$name")/DaysGone[.]exe" >/dev/null || { echo "stopped $name"; return 0; }
        sleep 2
    done
    die "$name did not exit"
}

# key <name> <xdotool key names...>, sent to the instance's own X display only.
cmd_key() {
    local name="${1:?instance name}" display
    shift
    need xdotool
    display="$(x_display "$name")"
    [ -n "$display" ] || die "$name has no display (is it running?)"
    DISPLAY="$display" xdotool key "$@"
}

# hold <name> <key> [seconds]: the game polls held keys, so a plain key tap can be missed.
cmd_hold() {
    local name="${1:?instance name}" key="${2:?key}" seconds="${3:-0.4}" display
    need xdotool
    display="$(x_display "$name")"
    [ -n "$display" ] || die "$name has no display (is it running?)"
    DISPLAY="$display" xdotool keydown "$key" sleep "$seconds" keyup "$key"
}

cmd_shot() {
    local name="${1:?instance name}" out="${2:?output png}" socket dir shot
    need gamescopectl
    socket="$(gamescope_socket "$name")"
    [ -n "$socket" ] || die "$name has no gamescope socket (is it running?)"
    # gamescopectl talks to GAMESCOPE_WAYLAND_DISPLAY (default gamescope-0), not WAYLAND_DISPLAY, and the
    # compositor writes the file itself, so a sandboxed instance would write into its private /tmp. Use a
    # path both sides see.
    dir="$(dir_of "$name")"
    shot="$dir/screenshot-$$.png"
    rm -f "$shot"
    GAMESCOPE_WAYLAND_DISPLAY="$socket" WAYLAND_DISPLAY="$socket" gamescopectl screenshot "$shot" >/dev/null
    for _ in $(seq 1 50); do
        if [ -s "$shot" ] && tail -c 12 "$shot" | grep -q IEND; then
            mv -f "$shot" "$out"
            echo "$out"
            return 0
        fi
        sleep 0.2
    done
    rm -f "$shot"
    die "no screenshot from $name"
}

cmd_debug() {
    local name="${1:?instance name}"
    shift
    python3 "$here/../debug_control.py" --game-dir "$(win64 "$name")" "$@"
}

cmd_log() {
    local name="${1:?instance name}"
    shift
    if [ $# -eq 0 ]; then set -- -n 40; fi
    tail "$@" "$(win64 "$name")/TheysGone.log"
}

# Passes the title splash and loads the newest save; waits until the player has health.
cmd_enter() {
    local name="${1:?instance name}" log timeout="${2:-600}" start
    log="$(win64 "$name")/TheysGone.log"
    start=$SECONDS
    # The world and player controller load behind the title splash.
    until grep -q "local controller:" "$log" 2>/dev/null; do
        [ $((SECONDS - start)) -lt "$timeout" ] || die "$name: the game did not load after ${timeout}s"
        sleep 3
    done
    until cmd_debug "$name" continue-title 2>/dev/null | grep -q '^ok'; do
        [ $((SECONDS - start)) -lt "$timeout" ] || die "$name: Continue never became available"
        cmd_key "$name" Return
        sleep 6
    done
    while :; do
        cmd_debug "$name" status >/dev/null 2>&1 || true
        sleep 1
        local line
        line="$(grep 'debug: status' "$log" | tail -1)"
        case "$line" in
            *playerHealth=0.000*|"") ;;
            *) echo "$name in gameplay: ${line#*debug: }"; return 0 ;;
        esac
        [ $((SECONDS - start)) -lt "$timeout" ] || die "$name: no gameplay after ${timeout}s"
        sleep 5
    done
}

# One-time: runs a sandboxed Steam client with its own data on the desktop so you can sign in to a
# second Steam account. Close it from its tray or the Steam menu when signed in.
cmd_steam_login() {
    local home="${1:?directory for the sandboxed Steam client}"
    need bwrap
    mkdir -p "$home"
    if [ ! -x "$home/.local/share/Steam/steam.sh" ]; then
        mkdir -p "$home/.local/share/Steam"
        tar -xf /usr/lib/steam/bootstraplinux_ubuntu12_32.tar.xz -C "$home/.local/share/Steam" ||
            die "could not unpack the Steam bootstrap"
    fi
    bwrap --dev-bind / / --tmpfs /tmp --bind /tmp/.X11-unix /tmp/.X11-unix --tmpfs /dev/shm --unshare-ipc \
        --die-with-parent --setenv HOME "$home" -- "$home/.local/share/Steam/steam.sh"
}

cmd_list() {
    [ -d "$LAB" ] || { echo "no lab at $LAB"; return 0; }
    for dir in "$LAB"/*/; do
        [ -d "$dir/game" ] || continue
        local name
        name="$(basename "$dir")"
        printf '%-12s %-8s %s\n' "$name" "$(running "$name" && echo running || echo stopped)" \
            "$(running "$name" && echo "display $(x_display "$name") socket $(gamescope_socket "$name")")"
    done
}

cmd_help() {
    cat <<EOF
lab.sh - headless Days Gone instances (lab: $LAB, base: $BASE)

  create <name>                  reflink-clone the base game copy and prefix
  config <name> Key=value...     set TheysGone.ini keys ([Coop] unless Section.Key=value)
  deploy <name> <TheysGone.asi>  install a build
  start <name> [--steam-home D]  run in headless gamescope; with --steam-home, inside a bwrap
                                 sandbox with that directory's Steam client and account
  enter <name> [timeout]         pass the title splash, Continue, wait for gameplay
  key <name> <keys...>           xdotool keys to the instance's own display
  hold <name> <key> [seconds]    hold a key (default 0.4 s); use for game controls
  shot <name> <out.png>          screenshot through gamescope
  debug <name> <command> [...]   tools/debug_control.py against the instance
  log <name> [tail args]         show TheysGone.log
  stop <name> | destroy <name> | list
  steam-login <dir>              one-time: sign a sandboxed Steam client in to another account
EOF
}

command="${1:-help}"
shift || true
case "$command" in
    create|destroy|config|deploy|start|stop|key|hold|shot|debug|log|enter|list|help) "cmd_$command" "$@" ;;
    steam-login) cmd_steam_login "$@" ;;
    *) die "unknown command $command (lab.sh help)" ;;
esac
