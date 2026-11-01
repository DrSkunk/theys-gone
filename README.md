![Theys Gone](https://raw.githubusercontent.com/DrSkunk/theys-gone/main/docs/theys_gone.avif)

[![CI](https://github.com/DrSkunk/theys-gone/actions/workflows/ci.yml/badge.svg)](https://github.com/DrSkunk/theys-gone/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/DrSkunk/theys-gone?include_prereleases&sort=semver)](https://github.com/DrSkunk/theys-gone/releases)
[![Nexus Mods](https://img.shields.io/badge/Nexus_Mods-Theys_Gone-orange)](https://www.nexusmods.com/daysgone/mods/1019)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

**Theys Gone** is an experimental two-player co-op mod for **Days Gone
(PC)**. It runs as an ASI plugin inside the game. The host's save is the one
that counts: the joining player loads it, fights the host's enemies and helps
the host progress. Host and join sessions through Steam or your local network.

> [!IMPORTANT]
> This is **not** full campaign co-op. Each player still runs their own copy of
> the game. The joining player starts from the host's save and is resynced when
> the host's story moves on, but missions, inventory and loot are not mirrored
> live. Back up saves: the guest's save guard can fail. LAN UDP is
> unauthenticated; use it only on a trusted network.

## Features and limitations

- See your partner's movement, animation, held items and motorcycle; ride as
  their passenger, find them on the map or teleport nearby.
- Play on the host's save, fight host-owned enemies and follow the host's time
  and weather. Friendly fire requires both players to opt in each session.
- `FreeRoam=1` on both PCs lets distant players simulate enemies in their own
  regions. A fight in progress can remain with its owner when players split or
  regroup. Guest-owned enemies attacking the host after regrouping are still
  unverified.
- `MissionObjectiveSync=1` on both PCs optionally shares completed objectives;
  starting and completing jobs still requires host-save catch-up.
- One-PC paired tests have shown Boozer and his bike as display replicas,
  story passenger-seat and stealth policies, mission weather, flashback
  hiding, and shared menu pause. These do not establish coverage for all
  missions or separate PCs.

Live mission state, inventory, pickups, doors and camp NPCs are not shared.
Up to 16 nearby hostile and friendly companion pawns share the stream limit,
so hordes are incomplete. Enemy melee damage is estimated; real aimed attacks
and ranged enemy damage need further testing. Projectiles and hit effects,
motorcycle physics, fuel, damage and upgrades are not synchronized. Your
partner uses your outfit model with their chosen colors.

The [co-op roadmap](https://github.com/DrSkunk/theys-gone/blob/main/docs/COOP-ROADMAP.md)
tracks the current source, exact test scope and next work.

## Requirements

- Days Gone on **Steam, build 19221447** (Patch 1.13, July 2025, which
  includes the free *Remastered* update). The mod checks the executable and
  disables itself on any other build, for example after a game update or on
  the `Classic` (1.07) beta branch. The *Broken Road* DLC modes have not been
  tested.
- Windows, or Linux / Steam Deck with Proton. It has been tested under Proton
  on a desktop Linux PC and a Steam Deck. Native Windows has not been tested.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
  (x64) to load the plugin.
- Two PCs with the same mod version. Connect over a trusted LAN or through
  Steam. Two-account Steam sessions have been tested between separate
  machines; Internet/NAT traversal has not been independently verified.

## Install

1. **Back up your saves.** They are in `%LOCALAPPDATA%\BendGame\Saved\` on
   Windows. Under Proton they are in
   `steamapps/compatdata/1259420/pfx/drive_c/users/steamuser/AppData/Local/BendGame/Saved/`.
2. Download `TheysGone-<version>.zip` from
   [Nexus Mods](https://www.nexusmods.com/daysgone/mods/1019) or
   [GitHub Releases](https://github.com/DrSkunk/theys-gone/releases).
   Use **Manual Download** on Nexus; Vortex installation is not supported.
3. Install Ultimate ASI Loader x64 as `version.dll` in the game's
   `BendGame/Binaries/Win64/` folder, next to `DaysGone.exe`. Skip this step if
   you already have an ASI loader there, and don't overwrite other mods.
4. Copy `TheysGone.asi` and `TheysGone.ini` from the zip into that same
   folder. Both files must stay side by side.
5. Copy the zip's `BendGame/Content/Paks/pakchunk99-TheysGone_Brand_P.pak`
   into the matching folder under your game installation. This replaces the
   title and main-menu logos with Theys Gone branding.
6. **Linux / Steam Deck only:** Wine must load the proxy `version.dll`. In
   Steam, open the game's *Properties → Launch Options* and set:

   ```sh
   WINEDLLOVERRIDES="version=n,b" %command%
   ```

   If you already have launch options, add to them instead of replacing them.

To verify a download:

```sh
gh attestation verify TheysGone-<version>.zip --repo DrSkunk/theys-gone
```

The release also includes a `.sha256` checksum file.

## Set up a session

Open **CO-OP** in the game's title menu or pause menu. These are native game
widgets, not the F8 settings overlay. You can host before loading a save or
open a session while already playing. Sessions currently hold **two players**.

### LAN

Choose **HOST LAN GAME**. On the other PC choose **JOIN LAN GAME**, then select
the host from the local-network list. Use **CLOSE SESSION** / **LEAVE SESSION**
to disconnect without restarting the game.

For automatic LAN hosting/joining instead of choosing in the menu, set
`Transport=udp` and configure:

**Host:** leave `PeerHost` empty.

```ini
[Coop]
Port=27777
Transport=udp
PeerHost=
PlayerName=Host
```

**Joining player:** set `PeerHost` to the host's LAN IP address or hostname.

```ini
[Coop]
Port=27777
Transport=udp
PeerHost=192.168.1.50
PeerPort=27777
PlayerName=Partner
```

- Allow inbound **UDP 27777** (or your `Port`) through the host's firewall,
  for example with `sudo ufw allow from 192.168.1.0/24 to any port 27777 proto udp`.
  **Never** forward this port to the Internet: the protocol has no encryption
  or authentication.
- With `HostSave=1` and an active guest sandbox, the host's save transfers
  automatically; see below.
- Settings are read when the game starts. Restart after editing the INI;
  switching sessions from CO-OP does not require a restart.

#### Playing on the host's save

With `HostSave=1` (the default), the host's save is the one that counts:

1. The host loads their game. A snapshot of their saves is sent to the guest.
2. The joining player connects from the title or pause menu. If the save
   guard starts successfully, guest save writes go to a throw-away sandbox
   (`TheysGone-guest-saves`), reset when first activated in that game run.
3. The host's save is downloaded (a few seconds for about 7 MB on a LAN) and
   loaded. If the joining player is already in the game, it reloads in place.
   They spawn next to the host.
4. Each time the host saves, the host's story state (jobs and missions) is
   compared. If the joining player's story differs, they download the new save
   and are reloaded onto it. Catch-up is deferred while the host is running a
   story mission and waits for a later host save.

> [!WARNING]
> The guard can fail without blocking the session, leaving real guest saves
> unprotected. After joining, check `TheysGone.log` for
> `save guard: guest save sandbox active until restart`. If it is absent or
> F8 says `Save sandbox failed; host save disabled`, stop playing and restore
> your backup if needed.

With the sandbox active, guest progress is disposable, including after the
host leaves, until the guest restarts the game. Set `HostSave=0` on both PCs
to play on separate saves instead. A cached snapshot can lag a recent host
save, so check progress after a transfer.

In one paired fast-travel test, the guest received the host's new position and
time but stayed about 466 m behind. Use teleport to regroup if needed.
Free-roam handoff at that distance and bedroll sleep have not been tested.

### Steam sessions (experimental)

> [!WARNING]
> Two-account Steam sessions reached gameplay on separate machines, but
> host-save transfer, invite acceptance and newer gameplay paths over Steam
> remain unverified. Internet/NAT traversal has not been independently verified.

Steam mode uses Steam Networking, so it does not require manual port
forwarding. Steam provides authenticated, encrypted transport and can use
NAT traversal or relaying. Actual Internet-path behavior has not been
independently verified. Leave `Transport=menu` (the default):

```ini
[Coop]
Transport=menu
PlayerName=
Visibility=friends
```

1. **Host:** choose **CO-OP → HOST ONLINE GAME**, then **INVITE FRIENDS** to
   open Steam's invite dialog. **VISIBILITY** cycles between invite-only,
   friends-only, and public; it can also be changed from the pause menu.
2. **Guest:** accept the Steam invite, choose **JOIN A FRIEND**, or use
   **FIND PUBLIC GAMES** for public mod lobbies.
3. At the title menu, **START GAME** invokes the game's normal Continue action
   on this PC. It does not remotely start the other player's game.
4. Steam persona names are used by default. Connection events appear in the
   lobby log and as native HUD notifications during gameplay. This is an
   event log, not typed text chat.

#### Spacewar (optional, untested)

By default the mod uses the game's own Steam connection, so lobbies belong to
Days Gone. `SteamApp=spacewar` instead runs the lobby under Spacewar (app 480),
Valve's public test app, through a second copy of the Steam API. Use it when a
supported game has no Steam connection of its own; the supported executable
requirement is unchanged.

1. Set `SteamApp=spacewar` in `TheysGone.ini`, and keep the Steam client running.
2. On first use the mod copies the game's `steam_api64.dll` next to the game
   executable as `steam_spacewar.dll`. If the game has no `steam_api64.dll`, or
   the folder is not writable, place a copy of one (from any Steamworks game)
   there under that name yourself; a file you put there is always used as-is.
   Delete `steam_spacewar.dll` to undo.

Both players must use the same `SteamApp`: Spacewar players only see Spacewar
lobbies. This mode has not been tested in a paired session yet.

Only traffic from the other lobby member is accepted. `Port` and `PeerHost`
do not control Steam connections. Epic Online Services is not implemented.
The lobby owner is the host: their save, story and (in standard mode) enemies
are authoritative. With `FreeRoam=1` on both peers, distant regions simulate
their own enemies; each side shows the other's enemies as replicas.

## Controls

| Action | Keyboard / mouse | Gamepad (XInput) |
|---|---|---|
| Open or close gameplay settings | F8 | View/Back + Y |
| Select a row | Up/Down or click | D-pad Up/Down |
| Change or activate | Left/Right, Enter or click | D-pad Left/Right or A |
| Close | Esc | B |
| Join or leave the partner's bike | F9 | View/Back + X |
| Teleport next to the partner | F7 (`TeleportKey`) | Menu |

The game window must be focused. The F8 settings overlay does not pause the
game. Session hosting/joining is in the native title/pause **CO-OP** menu.

While a partner is connected, the weapon/crafting wheel stays usable but its
slow-motion request is removed without intentionally changing focus or combat
slow motion.
Normal weapon-wheel slowdown returns when playing alone, including when a
partner disconnects while the wheel is still open. This is experimental:
paired testing on one PC passed; separate-PC and actual focus/combat overlap
are still untested.

While connected, opening the native pause menu, map or inventory keeps
gameplay running unless **both players** have one of these menus open.
Closing either player's menu resumes gameplay. When the partner disconnects,
normal single-player pausing returns. The F8 settings overlay and weapon
wheel do not count as pause requests. Both players need protocol v11.

On a **Steam Deck**, the mod reads XInput, not Steam Input actions. If the
gamepad shortcuts do nothing, pick the *Gamepad With Joystick Trackpad*
template under *Controller Settings → Current Layout → Templates*.

### Gameplay settings (F8)

The overlay controls appearance, motorcycle and enemy sync, passenger riding,
teleport and Steam lobby actions. Changes are saved to the INI immediately,
except friendly fire: both players must enable and confirm it each session.

### Passenger riding

Both players need *Sync motorcycles* on, and the driver needs *Allow a
passenger*. The driver gets on their bike and stops. The passenger walks up
to it and holds the game's own interact button on the **Ride with
&lt;partner&gt;** prompt (F9 also works). The passenger is attached to the bike
as a visual rider, and it isn't the game's own two-seat vehicle code. The
passenger can't attack, and saving is blocked while riding. Hold **Get off**
(or press F9) to dismount. An exit is refused when there is no safe ground to
stand on.

During story missions where a character rides on the driver's bike (Lisa,
Sarah, Boozer) or the driver rides on theirs, the seat is kept for the story:
the prompt does not appear, and a partner already riding is let off.

## Configuration reference

`TheysGone.ini` documents its `[Coop]` settings and defaults. `HostSave=1`
is on by default; `FreeRoam=1` and `MissionObjectiveSync=1` are experimental,
off by default and must be enabled on both PCs. The latter also requires
`HostSave=1`. Set `PartnerMarker=0` under `[Coop]` to hide the partner's
minimap and world-map marker; it defaults to `1`.

## Troubleshooting

The plugin writes `TheysGone.log` next to itself. Review and redact player
names, account IDs, IP addresses and local paths before sharing it. A healthy
start looks like this:

```text
Theys Gone … starting (protocol v11)
ProcessEvent hook installed
GNames via pointer at rva 0x…, … names
network: listening on UDP 27777, …
peer Partner (…) connected
avatar: spawned …
```

| Symptom | Likely cause |
|---|---|
| No log file | The ASI loader isn't loading. Check `version.dll` and, on Proton, the `WINEDLLOVERRIDES` launch option |
| `FATAL: unsupported DaysGone.exe` | Wrong game version. The mod disables itself; wait for an updated release |
| `Save sandbox failed; host save disabled` | Guest saves may be unprotected. Stop playing and check the log and your backup |
| Never connects | Wrong `PeerHost`/port, firewall, a mod version mismatch, or a player hasn't loaded a save |
| Connected but no partner visible | You are far apart (try F7), or the partner just loaded and the avatar is being rebuilt (wait about 5 seconds) |

Any `FATAL` line means the mod switched itself off and the game keeps running
normally.

## Uninstall

Close the game and delete `TheysGone.asi`, `TheysGone.ini` and the
`TheysGone*.log`/`.txt` files. Remove `version.dll`, and the launch option,
only if no other mod uses the ASI loader.
Delete `BendGame/Content/Paks/pakchunk99-TheysGone_Brand_P.pak` to restore
the original logos. If you enabled Spacewar, delete `steam_spacewar.dll` too.

Upgrading: both players must run the same release. Releases that change the
network protocol cannot connect to older ones; the changelog marks these as
breaking changes.

Upgrading from the older *Days Gone Together* builds: delete the old
`DaysGoneTogether.*` files first. Settings are not migrated, and old builds
cannot connect to Theys Gone.

## How it was built

The mod is a C++20 DLL that hooks Unreal Engine 4's `UObject::ProcessEvent`
and uses reflection to drive the game. The optional branding pak loads
alongside the original game files. See
[how it works](https://github.com/DrSkunk/theys-gone/blob/main/docs/HOW-IT-WORKS.md)
for the design and reverse engineering.

**Use of AI:** most of this project was developed with AI coding assistants
(GitHub Copilot and large language models). That covers code, the static and
runtime analysis of the game, tests and documentation. A human chose the
goals, reviewed the results and tested the mod in the real game. AI output
can contain mistakes: check claims against the code and your own tests, and
report anything wrong.

## Development

Building, testing and releases are covered in the
[development guide](https://github.com/DrSkunk/theys-gone/blob/main/docs/DEVELOPMENT.md).
Contributions are welcome; see
[CONTRIBUTING.md](https://github.com/DrSkunk/theys-gone/blob/main/CONTRIBUTING.md).

The save file research and the save editor (CLI and browser) live in the
separate [days-gone-save-editor](https://github.com/drskunk/days-gone-save-editor)
repository.

## Legal

Days Gone is © Sony Interactive Entertainment, developed by Bend Studio. This
is an unofficial fan project, not affiliated with or endorsed by Sony or Bend
Studio. You need your own copy of the game. The branding pak contains modified
logo textures; underlying game artwork remains the property of its owners.

The mod's code is licensed under the [MIT License](LICENSE); that license does
not grant rights to underlying game artwork. Release builds bundle
[Dear ImGui](https://github.com/ocornut/imgui) (MIT); its license is included
in the release archive.
