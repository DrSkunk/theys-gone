# Headless test lab

[`lab.sh`](lab.sh) runs Days Gone instances on one Linux/Proton PC without
touching the desktop or a real install, so paired co-op tests can be scripted.

- **Disposable clones.** `create` makes a reflink clone of an isolated game
  copy and its Proton prefix. Set `THEYS_GONE_LAB_BASE` to the folder holding
  that copy (`game/` and `prefix/`), and optionally `THEYS_GONE_LAB` for
  where clones go; `lab.sh help` prints the paths in use. Clones share disk
  blocks with the base, so they are instant and cost only what the test
  writes. The base is never written; `destroy` throws a clone away. Needs
  btrfs or XFS: `cp --reflink=always` refuses rather than making a 60 GB
  copy.
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

## Creating the base copy

The base is an isolated copy of the game and its Proton prefix; the lab never
reads the real install after this step. Put it on the btrfs or XFS volume
where clones will be made.

1. Close the game. Copy the install folder (for example
   `steamapps/common/Days Gone`) to `$THEYS_GONE_LAB_BASE/game`.
2. Copy the prefix `steamapps/compatdata/1259420` to
   `$THEYS_GONE_LAB_BASE/prefix`, so that `prefix/pfx` exists. This copies the
   saves too; the real ones are not touched afterwards.
3. Check the copy is the supported build: the SHA-256 of
   `game/BendGame/Binaries/Win64/DaysGone.exe` must match
   [`config/steam-19221447.json`](../../config/steam-19221447.json).
4. Leave the mod out of the base; `deploy` installs it per clone.

## Requirements

gamescope 3.16 or newer, `gamescopectl`, `xdotool`, `bwrap`, systemd user
services, Steam with Proton Experimental and Steam Linux Runtime 4
(`THEYS_GONE_STEAM`, `THEYS_GONE_PROTON` and `THEYS_GONE_RUNTIME` override
their paths). Two instances at 1280×720 used about 4.6 GB of video memory.

## Paired session

```sh
L=tools/lab/lab.sh
export THEYS_GONE_LAB_BASE=/path/to/isolated-copy
S=/path/to/second-steam-home            # any empty folder for the second account
$L steam-login "$S"                     # once: sign in to the second account
for n in host guest; do $L create $n; $L deploy $n package/TheysGone.asi; done
$L config host Transport=udp PeerHost= Port=27777 PeerPort=27777 DebugTools=1 Discovery=0 PlayerName=Host
$L config guest Transport=udp PeerHost=127.0.0.1 Port=27778 PeerPort=27777 DebugTools=1 Discovery=0 PlayerName=Guest
$L start host
$L start guest --steam-home "$S"
$L enter host && $L enter guest         # splash, Continue, wait for gameplay
$L debug host status                    # any tools/debug_control.py command
$L shot guest /tmp/guest.png
$L stop guest; $L stop host; $L destroy guest; $L destroy host
```

`enter` waits for the world to load, presses Enter until the title menu
offers Continue, then waits until the player has health. With `HostSave=1`
the guest then downloads the host's save and reloads into it. Both `enter`
and `debug` require `DebugTools=1` in the disposable instance.

The game polls held keys, so use `hold` for game controls (`$L hold host e`
mounts the bike, `$L hold guest F9` boards the partner's); `key` taps keys
for menus.

## Limitations

- The lab tests LAN UDP only. It cannot cover Steam transport work (lobbies,
  invites, Steam networking); use two PCs for that, with
  [`tools/twopc/`](../twopc/README.md).
- Lab runs are one-PC paired tests, not two-PC tests. Record each run in the
  [research log](../../docs/REPLICATION-RESEARCH.md) and status changes in
  the [co-op roadmap](../../docs/COOP-ROADMAP.md).
- The game still needs a Steam client per account; the sandboxed client does
  not expose itself to the mod's Steam transport, which is why paired lab
  sessions use `Transport=udp`.
- The splash needs one key press per instance; `enter` sends it, and stops
  if the title menu has no Continue rather than starting a new game.
- Saves live under `Saved/<SteamID64>`. `start` copies the base copy's saves
  for the account that will run the instance, read from that client's
  `loginusers.vdf`.
- The desktop's Steam client and the sandboxed one must be signed in to
  different accounts; check after a reboot, since the desktop client may
  auto-login to the other one.
