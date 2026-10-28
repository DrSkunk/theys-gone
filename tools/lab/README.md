# Headless test lab

[`lab.sh`](lab.sh) runs Days Gone instances on one Linux/Proton PC without
touching the desktop or a real install, so paired co-op tests can be scripted.

- **Disposable clones.** `create` makes a reflink clone of an isolated game
  copy and its Proton prefix (`THEYS_GONE_LAB_BASE`, default
  `~/TheysGone-coop-test/run1`). Clones share disk blocks with the base, so
  they are instant and cost only what the test writes. The base is never
  written; `destroy` throws a clone away. Needs btrfs or XFS: `cp
  --reflink=always` refuses rather than making a 60 GB copy.
- **Headless display.** `start` runs the instance in `gamescope --backend
  headless`. Each instance gets its own Xwayland display (`key` sends
  `xdotool` keys to it only) and gamescope socket (`shot` takes a screenshot
  with `gamescopectl`).
- **Second Steam account.** With `--steam-home DIR`, the instance and a
  second Steam client run together in a bubblewrap sandbox with a private
  `/tmp`, `/dev/shm` and IPC namespace, so that client and the desktop's run
  side by side. Sign that client in once with `steam-login DIR`; the window
  opens on the desktop. Without `--steam-home` an instance uses the desktop's
  Steam client. One Steam account cannot run two copies of the game: the
  second one hangs before Steam registers it.

## Requirements

gamescope 3.16 or newer, `gamescopectl`, `xdotool`, `bwrap`, systemd user
services, Steam with Proton Experimental and Steam Linux Runtime 4. Two
instances at 1280×720 used about 4.6 GB of video memory on an RTX 3070.

## Paired session

```sh
L=tools/lab/lab.sh
$L steam-login ~/TheysGone-lab/steam-second   # once: sign in to the second account
for n in host guest; do $L create $n; $L deploy $n package/TheysGone.asi; done
$L config host PeerHost= Port=27777 PeerPort=27777 Discovery=0 PlayerName=Host
$L config guest PeerHost=127.0.0.1 Port=27778 PeerPort=27777 Discovery=0 PlayerName=Guest
$L start host
$L start guest --steam-home ~/TheysGone-lab/steam-second
$L enter host && $L enter guest         # splash, Continue, wait for gameplay
$L debug host status                    # any tools/debug_control.py command
$L shot guest /tmp/guest.png
$L stop guest; $L stop host; $L destroy guest; $L destroy host
```

`enter` waits for the world to load, presses Enter until the title menu
offers Continue, then waits until the player has health. With `HostSave=1`
the guest then downloads the host's save and reloads into it.

The game polls held keys, so use `hold` for game controls (`$L hold host e`
mounts the bike, `$L hold guest F9` boards the partner's); `key` taps keys
for menus.

## Limitations

- The game still needs a Steam client per account; the sandboxed client does
  not expose itself to the mod's Steam transport, so paired lab sessions use
  `Transport=udp`.
- The splash needs one key press per instance; `enter` sends it.
