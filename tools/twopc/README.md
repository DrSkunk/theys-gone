# Two-PC test tooling

[`twopc.sh`](twopc.sh) runs a two-PC test session from a dev machine over
ssh: back up and install a build on each PC, start the game, drive its menus
with keys, take screenshots, read the logs, then restore and verify every
file. It covers what the [headless lab](../lab/README.md) cannot: the Steam
transport, real network timing and separate hardware such as a Steam Deck.

Use it only on dedicated test PCs that you own. It works on the PC's own
Steam install, so `prep` and `restore` are mandatory around every run (see
[Live testing](../../AGENTS.md#rules)), and its key taps go to whatever has
focus.

## Requirements

- **Dev machine:** bash and ssh/scp. Key-based ssh is easiest; for anything
  else, point `THEYS_GONE_SSH` and `THEYS_GONE_SCP` at a wrapper that takes the
  same arguments. Never commit a password file.
- **Each test PC:** Linux with Proton and Days Gone in the default Steam
  library (`THEYS_GONE_STEAM_ROOT` overrides it), `python3` with
  `python-evdev`, and write access to `/dev/uinput` for the logged-in user
  (SteamOS and KDE Plasma desktops grant it to the seat user). Screenshots
  use `spectacle` on a KDE Plasma desktop and `gamescopectl` in Steam Deck
  game mode; set `THEYS_GONE_CAPTURE=spectacle|gamescope` to override.
  Remote commands run through `bash`, so a fish login shell is fine.

## Session

```sh
T=tools/twopc/twopc.sh
export THEYS_GONE_HOST_SSH=user@host-pc THEYS_GONE_GUEST_SSH=user@guest-pc
LABEL=$(date +%Y%m%d)-steam             # one backup label per run

make build
$T setup both                           # copy remote.sh and vkey.py to ~/.cache/theys-gone-twopc
$T status both                          # game closed? which ASI and INI?
$T prep both "$LABEL" package/TheysGone.asi
$T launch both
$T keys both enter sleep:12000          # splash, then wait for the title menu
$T keys both down down down down down enter   # title menu -> CO-OP
$T shot both /tmp/coop.png              # writes /tmp/coop-host.png and /tmp/coop-guest.png
$T keys host enter                      # HOST ONLINE GAME
$T keys guest down enter                # JOIN A FRIEND
$T log guest 'save transfer|session event'
# ... the scenario ...
$T keys both esc sleep:2500 down down down down down down down enter sleep:2000 enter  # QUIT TO DESKTOP
$T quit-wait both
$T restore both "$LABEL"                # fails loudly if any hash differs
```

`PC` is `host`, `guest` or `both`. `keys` takes `vkey.py` steps: key names
(`up`, `down`, `left`, `right`, `enter`, `esc`, `space`, `tab`, `a`–`z`,
`0`–`9`, `f1`–`f12`), `KEY:MS` to hold a key, and `sleep:MS` to wait. Menus
drop taps that come too quickly, so `vkey.py` leaves 0.35 s between taps.
Check a screenshot before any confirming key: a menu that opened with a
different row selected turns `enter` into another action.

## What each step does

- **`prep LABEL ASI`** refuses to run while the game is running or when
  `~/TheysGone-backup-LABEL` already exists. It saves `TheysGone.asi`,
  `TheysGone.ini`, the previous `TheysGone.log` and a tar of the whole Proton
  `Saved` tree with SHA-256 lists, then installs the ASI with a new inode and
  removes the INI, so the run uses built-in defaults as a new player would.
  To test with INI keys, write them on the PC after `prep`.
- **`restore LABEL`** first keeps the post-test log and `Saved` tree in
  `~/TheysGone-backup-LABEL/after/`. It then puts the ASI, INI, log and
  `Saved` back and compares every hash with the backup, failing on any
  difference. Keep the backup folder until the run is written up.
- **Shader pre-caching:** with Steam's "Enable Shader Pre-Caching" on, a
  launch can stop for minutes at a "Processing Vulkan shaders" dialog: each
  session records new pipelines, which Steam replays before the next start.
  Turn it off in Steam → Settings → Downloads on dedicated test PCs (global
  for every game); Steam then writes `"DisableShaderCache" "1"` under
  `ShaderCacheManager` in `~/.local/share/Steam/config/config.vdf`, and
  `status` reports it.
- **`launch`** starts the game through the running Steam client
  (`steam://rungameid/1259420`) and waits up to 60 s for its process.
- **`quit-wait`** waits for the game to exit after you quit it through its
  menu. Quit through the menu rather than killing the process, so no save is
  cut off mid-write.

"Running" means the real install's `DaysGone.exe` only; headless lab clones
on the same PC are ignored.

## Verified

On 2026-11-06, between a KDE Plasma desktop host and a Steam Deck guest in
game mode, Steam transport: menu navigation by uinput keyboard on both PCs,
both capture backends, launching through Steam, quitting through the menu,
and backup and restore with matching hashes. The steps above were then put
into these scripts and rechecked with `setup`, `status`, `shot`, `log` and a
`prep`/`restore` round trip on the Steam Deck. A full session has not yet
run through `twopc.sh` itself. A virtual gamepad has not been shown to reach
the game on the Steam Deck; use keys there.

Record each run in the [research log](../../docs/REPLICATION-RESEARCH.md) as
a two-PC run, without player, account or host names.
