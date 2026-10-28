# Development

## Prerequisites

| Tool | Used for | Install |
|---|---|---|
| MinGW-w64 (`x86_64-w64-mingw32-g++`, GCC 13 or newer, posix threads) | Plugin build | macOS: `brew install mingw-w64`. Debian/Ubuntu: `apt install g++-mingw-w64-x86-64-posix` and set `CXX=x86_64-w64-mingw32-g++-posix` |
| A native C++20 compiler | Protocol tests | Xcode command-line tools, `g++` or `clang++` |
| Python 3.11+ and [Poetry](https://python-poetry.org/) | Tool tests and analysis | `pipx install poetry` |
| `curl`, `shasum`, `zip` | Fetching Dear ImGui, packaging | usually preinstalled |

## Build and test

```sh
poetry install
make test     # C++ protocol/motion/combat tests + Python unit tests
make build    # package/TheysGone.asi + TheysGone.ini + branding pak
make dist     # dist/TheysGone-<version>.zip + .sha256
```

The build downloads the pinned Dear ImGui release into `.deps/` and checks its
SHA-256 against [`dependencies/imgui.sha256`](../dependencies/imgui.sha256).
It never touches a game installation.

Local builds report `<version.txt>-dev` in the log. Release builds set
`COOP_VERSION` to the release version. Linking uses `--no-insert-timestamp`,
so the same compiler and sources produce the same binary.

Tests cover the C++ packet decoder and all datagram types, sequence and
timestamp wraparound, friendly-fire consent and retransmission, motion
smoothing under jitter and packet loss, the debug command gate, and the
Python simulator and analysis tools. They don't replace testing in the game.

## Source layout

| Path | Role |
|---|---|
| [`src/coop/dllmain.cpp`](../src/coop/dllmain.cpp) | DLL entry, `InitializeASI` |
| [`src/coop/coop.cpp`](../src/coop/coop.cpp) | Build check, hook, per-frame logic, all features |
| [`src/coop/ue.hpp`](../src/coop/ue.hpp), [`ue.cpp`](../src/coop/ue.cpp) | Build-specific UE4 Bend reflection layer |
| [`src/coop/protocol.hpp`](../src/coop/protocol.hpp) | State packet |
| [`src/coop/pose.hpp`](../src/coop/pose.hpp), [`skeleton.cpp`](../src/coop/skeleton.cpp) | Pose compression, reading and writing skeletons |
| [`src/coop/gear.hpp`](../src/coop/gear.hpp), [`enemy.hpp`](../src/coop/enemy.hpp) | Held-item and enemy datagrams |
| [`src/coop/motion.hpp`](../src/coop/motion.hpp), [`frameclock.hpp`](../src/coop/frameclock.hpp) | Interpolation and frame clock |
| [`src/coop/combat.hpp`](../src/coop/combat.hpp) | Friendly-fire delivery queue |
| [`src/coop/appearance.cpp`](../src/coop/appearance.cpp) | Garment tinting |
| [`src/coop/menu.cpp`](../src/coop/menu.cpp), [`overlay.cpp`](../src/coop/overlay.cpp) | Menu input and DX11 overlay |
| [`src/coop/native_ui.cpp`](../src/coop/native_ui.cpp), [`session.hpp`](../src/coop/session.hpp) | Native title/pause CO-OP pages, event log and pure session/LAN discovery model |
| [`src/coop/net.cpp`](../src/coop/net.cpp), [`log.cpp`](../src/coop/log.cpp) | Transport interface, UDP socket and log |
| [`src/coop/steam.cpp`](../src/coop/steam.cpp), [`steam_abi.hpp`](../src/coop/steam_abi.hpp) | Experimental Steam lobby and networking transport (runtime-resolved, no SDK) |
| [`src/coop/save_guard.cpp`](../src/coop/save_guard.cpp), [`save_policy.hpp`](../src/coop/save_policy.hpp) | Experimental guest save sandbox (import hooks and pure path rules) |
| [`src/coop/save_transfer.cpp`](../src/coop/save_transfer.cpp), [`savesync.hpp`](../src/coop/savesync.hpp), [`sha256.hpp`](../src/coop/sha256.hpp) | Experimental host-to-guest save transfer (runtime, pure wire/bundle format, SHA-256) |
| [`src/coop/debug_commands.hpp`](../src/coop/debug_commands.hpp) | Debug command allowlist |
| [`tools/`](../tools) | Tests, simulator, debug helper, offline analysis |

[HOW-IT-WORKS.md](HOW-IT-WORKS.md) explains the design.

## Protocol changes

Both players must run the same protocol version. Any change to a packet layout
or its meaning must bump `kProtocolVersion` in `protocol.hpp`, or the
datagram's own version constant, and update `tools/test_protocol.cpp` and
`tools/fake_peer.py`. Mark such commits as breaking (`feat!:`).

## Headless test lab

[`tools/lab/lab.sh`](../tools/lab/lab.sh) runs two real games on one Linux
PC for paired tests: disposable reflink clones of an isolated copy, each in
its own headless gamescope, the guest in a bubblewrap sandbox with a second
Steam account. See [`tools/lab/README.md`](../tools/lab/README.md).

## Simulated partner

[`tools/fake_peer.py`](../tools/fake_peer.py) speaks the protocol, so you can
test with a single game.
It also reports fresh version-3 host story packets (saved/live signatures,
save serial and sequence) and fresh completed-objective packets, allowing both
story protocols to be observed without a second game client.

**On a Linux/Proton machine with systemd:** copy `tools/fake_peer.py` and
`tools/same-pc/*.sh` into one folder on the gaming PC and run:

```sh
./start-test.sh            # follow (default) | circle | still
./stop-test.sh             # stop the simulator and restore the INI
```

`start-test.sh` backs up the INI and points it at `127.0.0.1:27778`. It runs
the simulator as the `theys-gone-fakepeer` user service and launches the game.
`DAYS_GONE_DIR` overrides the game folder. Restart the game after stopping.

**By hand,** against a game in host mode:

```sh
poetry run python tools/fake_peer.py --game 192.168.1.50:27777 \
  --port 27778 --offset 300 --radius 150 --speed 150 --duration 60
```

Units are centimeters. `--speed 0` gives a stationary partner and `--follow`
trails the player. Run `--help` for all options.

`--serve-save DIR` makes the simulator a host that shares the `*.sav` files in
`DIR` with a joining game (`HostSave=1`, `PeerHost=127.0.0.1`, `PeerPort=27778`).
`--fetch-save DIR` makes it a guest that downloads a `HostSave=1` host game's
saves into `DIR` and prints their SHA-256.

## Branding

The validated binary asset is tracked at
[`assets/branding/pakchunk99-TheysGone_Brand_P.pak`](../assets/branding/pakchunk99-TheysGone_Brand_P.pak).
`make build` copies it into `package/BendGame/Content/Paks/`; `make dist`
includes that same game-relative path in the archive. There is currently no
source-to-pak regeneration tool in the repository.

`UI_MainMenuWidget_C` shows two logo images: `Logo` (`title_logo_start`, the
title splash) and `LogoSmall` (`title_logo_start_small`, the menu after the
splash). The branding pak `pakchunk99-TheysGone_Brand_P.pak` replaces both
textures, but the game keeps drawing the original small texture. The plugin
therefore points `LogoSmall` at `Logo`'s texture at its own brush size, logged
as `native ui: title menu logo uses the branded title texture`. Without the pak
both textures are the original Days Gone logo, so the swap changes nothing.

## Debug tools

The plugin includes local test tools for spawning enemies, invulnerability,
damage probes, and stepping the time and weather. They are meant for checking
features, **not** as a trainer.

Enable them with `DebugTools=1` in the INI and restart. Then send commands with
the standard-library helper, which needs local access to the game folder:

```sh
poetry run python tools/debug_control.py --game-dir "/path/to/BendGame/Binaries/Win64" begin
poetry run python tools/debug_control.py --game-dir "/path/to/BendGame/Binaries/Win64" spawn-single
poetry run python tools/debug_control.py --game-dir "/path/to/BendGame/Binaries/Win64" end
```

| Command | Effect |
|---|---|
| `inspect` | Log a report of native cheat, spawn and horde APIs and AI objects |
| `begin` | Start a test session: capture state, block native saving |
| `god-on` / `god-off` | Make the player invulnerable / vulnerable |
| `spawn-single` | Spawn one swarmer with its native AI, about 6 m ahead |
| `spawn-pack` | Spawn eight independent swarmers (at most 16 owned at a time) |
| `status` | Log player and enemy health, controllers and movement, and the running story mission code and policy flags |
| `continue-title` | In-game bot: run the visible title splash's native button animation, or invoke Continue only when it is the first visible title-menu row |
| `ui-probe` | Log native startup/menu objects, visibility, option counts, first label, cached versus freshly queried save availability, and every `Image` in an on-screen widget with its brush texture; does not activate a menu action |
| `probe-player` / `probe-enemy` | Apply 5 base damage and check the health change |
| `enemy-health-all-on` | Host: enable `bReplicateHealthToAll` only on this session's owned test enemies |
| `kill-owned-enemy` | Host: apply bounded lethal native damage to one authoritative, session-owned test enemy and verify death; never selects ambient enemies or players |
| `time-shift` | Host: advance the time of day by 6 hours |
| `weather-next` | Host: switch to the next weather state |
| `cleanup` | Destroy only the enemies this session spawned |
| `end` | Clean up and restore damage and saving state |
| `hit-enemy` | Send 25 damage to the nearest live replica of the partner's enemies, as a real hit would (the guest hits host enemies; in free roam the host can hit guest-owned enemies) |
| `story-probe` | Log the job manager's jobs and missions and this PC's story signature |
| `story-diverge` | Guest with the sandbox active: pretend the story differs, to force a resync |
| `reload-save` | Reload the newest save in place (`ReloadBecauseOfJobFailure`) |
| `ride-test` | Toggle a **Ride with** prompt 3 m ahead, to test the native interact prompt alone |
| `dump-reflection` | Write every loaded class, property and function to `TheysGone-reflection.txt` |
| `dump-objects` | Write a census of live objects to `TheysGone-objects.txt` |
| `net-probe` | Write the engine's built-in networking state (net drivers, connections, controllers, roles, replicated actors and properties) to `TheysGone-net.txt` |
| `net-listen` | Experimental, host: start the engine's built-in listen server (Steam P2P) on the live world; `[Research] ControllerClass=`, `PawnClass=`, `SpectatorJoin=` set what joining players get |
| `net-connect` | Experimental, guest: join a built-in listen server at `[Research] ConnectUrl` (`steam.<host SteamID64>`) without reloading the map; `[Research] ClientNavigation=1` recreates the world's navigation system after the join |
| `net-possess` | Experimental, host: spawn a pawn (`[Research] GuestPawnClass`, default `BendDefaultPlayerPawn_C`) 3 m from the host and let the joined player's controller possess it |
| `net-takeover` | Experimental, guest: make the replicated controller local so this PC drives the host-owned pawn; run again to switch back. It also rebuilds the input stack for the taken-over controller, skips the own controller's `ProcessPlayerInput`, adopts the own controller's Blueprint input components and moves the taken-over controller to player index 0 (Bend's camera and many Blueprints use player 0) |
| `net-diag` | Experimental: log roles, ownership (`GetNetConnection`), input components, control rotation and tick state of every player controller and player pawn |
| `net-auto` | Experimental, guest: toggle the taken-over pawn's local role between SimulatedProxy and AutonomousProxy |
| `net-nudge` | Experimental, guest: call `AddYawInput(30)` on the local controller to test input without the viewport |
| `net-input` | Experimental: first run wraps `PlayerController` InputKey/InputAxis, UpdateRotation, AddYawInput, ProcessPlayerInput, the player pawn look handlers and the camera's rotation input; later runs log and reset per-controller counts, rotations, the static input stack and camera manager state |
| `net-walk` | Experimental: feed `AddMovementInput` to the local controller's pawn every frame for 3 s and log the distance moved plus movement telemetry (velocity, acceleration, desiredSpeed, root motion) every 0.5 s. With `[Research] WalkTarget=remote` the host walks the pawn of the remote guest's controller instead |
| `trace-on` / `trace-off` | Record every distinct ProcessEvent call with sample parameters to `TheysGone-trace.txt` (`OnRep_*`, `ReceiveBeginPlay` and `ClientRestart` are counted per object) |
| `mission-watch-on` / `mission-watch-off` | Mission lab: log mission/job ProcessEvent calls with parameter values, poll job state (done, active, state, objective index, tracked job, episode) every 0.5 s and log each change; `off` prints per-function call counts |
| `mission-state` | Read-only mission lab: write every job and mission row, the tracked job, the episode and active job objects to `TheysGone-missions.txt`; objective arrays include reflected struct fields and values. A catalog section lists every job (row name, display name, act, objective texts) and every story mission from `BendGameMission.GetMissionDataFromIDNew` (display name, act, map, prerequisites, objectives), plus the chapter names. Does not require a gameplay pawn if the job manager is loaded |
| `mission-act` | Mission lab, **changes the story**: call one mission function from `--arg "<Function> [ObjectName\|-] [Param=Value ...]"` and log the return value and the immediate state change. Use `Class.Function` when targeting a class. Requires `[Research] MissionActions=1`; back up saves first, because the game may autosave the result |
| `stream-focus` | Free roam: with `--arg "<east m> <north m>"`, add a debug second streaming focus at that offset from the local pawn (more than 150 m away); `--arg off` clears it. Needs `FreeRoam=1` at launch and the streaming hook installed |
| `stream-probe` | Free roam: log the separation state, the second focus, the last tile-merge counts, loaded levels, ground at the focus and AI within 150 m of it |
| `move-by` | Free roam: teleport the local pawn by `--arg "<east m> <north m>"` (each within 20 km) and hold it until a capsule sweep finds ground (30 s limit). Debug only; a large jump can land in unloaded terrain, so move in steps of a few hundred metres |

`hit-enemy`, `story-*`, `reload-save`, `ride-test`, `dump-*`, `net-*`, `trace-*`,
`stream-*`, `move-by` and `mission-*` work without `begin`. The research commands only read and log, except
`mission-act` and the experimental `net-*` commands, which drive the engine's own
networking; see [REPLICATION-RESEARCH.md](REPLICATION-RESEARCH.md).

`mission-act` looks the function up on the live `BendJobManager`, then
`BendGameMission`, unless a class is named (`IND_Jobscript_C.MedBoxObtained`).
A named class with no live instance falls back to its default object for static
functions only (`BendGameplayStatics.SaveGameOW - bCriticalSave=1`).
`ObjectName` fills the first object parameter by exact object name (for example
`IND-JobData`). Named parameters may be Bool, Byte, Int, Float, Name or Object.
The parameters are logged again after the call, which shows out values.
For example, `IND_Jobscript_C.UpdateJobObjectiveStatusAndProgression -
ObjIdentifier=IND3 ObjState=0` targets the class method; do not separate the
class and function with a pipe.

The experimental `net-*` path reads these `[Research]` options from
`TheysGone.ini` at startup (all off by default):

| Option | Side | Effect |
|---|---|---|
| `ClientNavigation=1` | guest | Recreate the navigation system after `net-connect`, so Bend's door code does not crash |
| `PawnReplication=1` | both | Restore the stock `ACharacter::GetLifetimeReplicatedProps` on Bend's character classes, which Bend replaced with an empty function. Without it, pawns replicate nothing after spawning |
| `BendHealthReplication=1` | both | Requires `PawnReplication=1`: additionally register `BendPawn.Health` with no lifetime condition. Set identically before `net-listen`/`net-connect` and restart both games to change it. This tests property transport only; Bend's native `OnRep_Health` virtual implementation is empty |
| `ClientPossess=1` | guest | Run Bend's `Possess` on the client during `net-takeover`, which creates the player navigation object (`CancelCurrentPlayerNavigation` crashes without it) |
| `ClientAuthoritativePosition=1` | host | Accept the guest's position (`ClientAuthorativePosition`, `MAXPOSITIONERRORSQUARED=1e12`). Bend moves by animation root motion, which the server does not reproduce for a remote pawn |
| `GuestPawnClass=` | host | Pawn class for `net-possess` |
| `WalkTarget=remote` | host | `net-walk` drives the remote guest's pawn |
| `WalkConsume=1` | any | `net-walk` consumes the movement input itself after each step |

Commands go through a file mailbox. The helper atomically writes
`TheysGone-debug.ini` (`[Debug] Command=…, Request=<new number>`) next to
the ASI. The plugin polls it every 0.5 s on the game thread and writes
`TheysGone-debug-result.ini`. Requests already present at startup are
ignored. Only the commands above are accepted, and nothing can be sent over
the network.

A session needs a living player on foot, outside missions and cinematics.
It ends automatically on death, a pawn change, mounting a bike, a mission or
cinematic, or after five minutes without a command. Packs are **not** native
hordes. The game's horde functions are tied to saved horde progress, so they
are deliberately not used. Blocking saves reduces risk but is not a sandbox:
use a backed-up or disposable save.

`kill-owned-enemy` reports an error if native damage does not actually kill
its owned target. A grapple/melee death-blow guard can clamp Health to 1;
for an isolated death-replication test, damage a fresh enemy before it
engages a player rather than bypassing that guard. `BendHealthReplication`
can deliver final Health and tear-off without restoring the guest's mesh,
dying animation, or corpse cleanup; see runs N/O in
[REPLICATION-RESEARCH.md](REPLICATION-RESEARCH.md).

## Remote game-session controller

[`tools/game_remote.py`](../tools/game_remote.py) is a small Linux/Windows helper for
controlling an active game session and retrieving screenshots. It is separate
from the mod and exposes only health, PNG capture, and a bounded allowlist of
keyboard taps, relative mouse moves, mouse clicks, virtual gamepad taps/stick
and trigger input, and waits. It has no shell, file, process or
arbitrary-command endpoint. Each tap releases its input and each analog input
returns to neutral even if an action fails.

**Linux:** the gaming PC needs Python 3.11+, `ydotool`/`ydotoold`, and either `grim`
(Wayland, including compatible gamescope sessions) or `scrot` (X11). The
`ydotoold` service and `/dev/uinput` permissions must be configured for the
same user/session that runs the helper. Virtual gamepad support additionally
requires `python-evdev` and working `/dev/uinput` access; `/v1/health` reports
whether it initialized. Screenshot capture may depend on the compositor
allowing capture of the game display; verify it in the actual session before
relying on it.

For KDE Wayland, `--capture spectacle` uses the installed KDE screenshot
utility without opening its UI. For an X11/XWayland game (including Proton
under Deck Gaming Mode), `--capture ffmpeg-x11` uses FFmpeg's `x11grab`.
It requires the game's `DISPLAY` and a readable `XAUTHORITY` from the same
session. Paths inside a Steam container may not exist in the host namespace.
XWayland capture sees X11 content, not native Wayland windows or Steam's
compositor overlays; inspect the returned PNG before relying on it.
On the tested Deck, it returned a black image even with the game running.
Use `--capture gamescope` with `WAYLAND_DISPLAY=gamescope-0` for Gaming Mode
instead: it uses the installed `gamescopectl screenshot` command and waits
for a complete PNG, capturing the compositor's actual game output.

**Windows:** Python 3.11+ is sufficient for keyboard/mouse input and PNG
screenshots. The helper automatically selects native `SendInput` (physical
scan codes and relative mouse events) and Windows PowerShell/System.Drawing
capture; `ydotool`, `grim` and `scrot` are not needed. `--capture windows`
selects this explicitly. Run it in the logged-in, unlocked gaming session,
with the game focused and at the same elevation level. Windows can block
input into a more privileged application. Prefer borderless/windowed mode
if exclusive fullscreen produces black screenshots.

Windows virtual Xbox 360 input is optional: install the Poetry extra with
`poetry install --extras remote-windows`, provision its ViGEmBus driver, and
pass `--windows-gamepad` when serving. ViGEmBus is a legacy, retired driver;
it is not needed for keyboard/mouse control, and the helper never installs
drivers itself. Enabling gamepad support without a working package/driver
fails at startup rather than pretending the controller is connected.
`/v1/health` reports the selected input/capture backends and gamepad status.
Stick Y is down-positive on both platforms; Windows maps it to XInput's
opposite convention.

Create an auth token on each PC and a TLS certificate with that machine's LAN
IP or DNS name in its subject alternative names. Keep the private key and
token private; securely copy the token and public certificate to the
controller machine, not the TLS private key. Certificates can be generated
on a machine with OpenSSL and securely provisioned to the gaming PC:

```sh
python3 tools/game_remote.py create-token ~/.config/theys-gone-remote.token
openssl req -x509 -newkey rsa:3072 -sha256 -days 30 -nodes \
  -keyout remote.key -out remote.crt \
  -subj "/CN=days-gone-test-pc" \
  -addext "subjectAltName=IP:192.168.1.50,DNS:days-gone-test-pc"
chmod 600 remote.key
python3 tools/game_remote.py serve --host 0.0.0.0 --port 8765 \
  --token-file ~/.config/theys-gone-remote.token \
  --capture grim --tls-cert remote.crt --tls-key remote.key
```

Use `--capture scrot` for an X11 session. The server refuses non-loopback
listeners without TLS and requires the token for every endpoint. The
controller trusts the certificate explicitly:

```sh
python3 tools/game_remote.py control --url https://192.168.1.50:8765 \
  --token-file ./pc.token --ca-cert ./remote.crt health
python3 tools/game_remote.py control --url https://192.168.1.50:8765 \
  --token-file ./pc.token --ca-cert ./remote.crt screenshot ./host.png
python3 tools/game_remote.py control --url https://192.168.1.50:8765 \
  --token-file ./pc.token --ca-cert ./remote.crt actions \
  '[{"type":"key","key":"w","duration_ms":300},{"type":"mouse_move","dx":30,"dy":0}]'
```

On Windows, create the token and start the helper from PowerShell after
provisioning the TLS files (the certificate must contain this PC's actual
IP/DNS name):

```powershell
$token = "$env:LOCALAPPDATA\TheysGone-remote.token"
py -3 tools/game_remote.py create-token $token
$identity = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
icacls $token /inheritance:r /grant:r "$($identity):(F)"
icacls .\remote.key /inheritance:r /grant:r "$($identity):(F)"
py -3 tools/game_remote.py serve --host 0.0.0.0 --token-file $token --tls-cert .\remote.crt --tls-key .\remote.key
```

Use a private-network-only firewall rule if Windows prompts or blocks the
port; do not disable the firewall. The client can run on Linux, Windows or
macOS, independently of which OS the gaming PC runs.

Gamepad actions use names such as `south`, `start`, or `lb`; stick coordinates
range from -32768 to 32767 and automatically return to center after the
requested duration:

```sh
python3 tools/game_remote.py control --url https://192.168.1.50:8765 \
  --token-file ./pc.token --ca-cert ./remote.crt actions \
  '[{"type":"gamepad_stick","stick":"left","x":0,"y":24000,"duration_ms":400},{"type":"gamepad_button","button":"south","duration_ms":100}]'
```

An omitted `--host` listens only on loopback, and plain HTTP is accepted only
for loopback control. Keep the helper on a trusted private network, stop it
when testing is over, and do not forward its port publicly. Test health,
screenshot, a short movement tap, a camera turn and input release on each
machine before using it for replication experiments. Its action contract is
covered by `make test`; screenshots and input injection still require
verification on the actual gaming compositor.

## Native CO-OP menus and discovery

`MenuDiscovery=1` enables a read-only native UI report. Press **F6** while
the main menu or pause menu is visible to write
`TheysGone-menus.txt` beside the plugin. Each capture replaces that
report. It lists loaded menu/widget classes, their reflected functions and
parameters, and live widget references. It also logs the main menu's
confirm, page-selection and mouse-row events. This diagnostic is separate
from the native CO-OP integration, which is enabled without it.

On build 19221447 under Proton, the main menu is the UMG instance
`UI_MainMenuWidget_C` from
`/Game/Libraries/UI/Blueprints/Menus/MainMenu/UI_MainMenuWidget`.
Its native assets include `MenuInteract_MainMenu0` through
`MenuInteract_MainMenu9` and `TextEntry0` through `TextEntry9`.
The observed main page had six options, ten text fields and
`MaxMenuOptions=6`. Relevant reflected entry points include
`OnConfirmPressed`, `ConfirmSelection`, `SetCursorPosition`,
`UpdateSelectedPageOptions` and `MainLabelUpdate`.

The plugin inserts CO-OP above the title menu's quit row, inside the existing
`MenuOptionNames_Text` array, without reallocating engine storage. While open,
co-op pages temporarily replace those labels. The engine still draws the
widgets, focus, sounds and mouse targets; confirm/back actions are intercepted
and routed to the session model. Start restores the original rows and invokes
the game's Continue action. This starts only the local game, not both clients.

The pause menu uses a different widget, `OptionsTopMenuWidget_C`, and its
`OptionsCategoryWidget` list (`UI_CategoryMenuWidget_C`). CO-OP is inserted above
the two quit options. Highlight/activate callbacks after that index are remapped
to the original `ActiveMenuProfile` index. Pages reuse the native category
entries, `OptionsTitle`, and `SetSelectionStatusText`. Fresh FText references
must be created before shifting category labels: shallow copies can lose
their last reference while the engine replaces the preceding entry.

`session.hpp` holds the menu rows, bounded event log, visibility rules and
versioned `TGGB` discovery beacons, covered by `make test`. `Transport=menu`
starts offline and lets CO-OP choose LAN or Steam. `Transport=udp` retains
automatic LAN startup from `PeerHost`. LAN hosts broadcast their name, port,
session and open/full state once per second; stale entries expire after 4 s.
Steam supports invite-only/friends/public visibility, friend listings and
filtered public search, with a two-member limit. Steam joining uses the
Steam API, not an IP-address connection. F8 remains the separate gameplay
settings/debug overlay.

The event log records hosting, connecting, connected and disconnected/left
events. Lobby pages show recent events. The title log and transient gameplay
log are native UMG TextBlock/Image children, using the game's font; the pause
menu uses its existing status-text widget. The gameplay log expires after
six seconds. This is not a typed chat protocol.

The title panel is attached to `Menu_Layout`; the gameplay panel is attached
to `UI_HudWidget_C.CanvasWrapper` on the left edge, between the objective
tracker and the health/weapon widgets, and copies the font of the HUD's own
`WarningText`. Every event is also written to the log as `session event: ...`.
Disconnects are reported once, for the name that was announced as connected. Widgets are constructed with reflected
`GameplayStatics.SpawnObject`, owned by the respective WidgetTree, attached
with `AddChildToCanvas`, and retained against garbage collection. Calling the
stock HUD `AddNotification` wrapper did not visibly display these messages,
so the implementation does not rely on that path. Failed notification
dispatch is logged and retried rather than silently acknowledged.

Title and pause entry rendering, keyboard navigation, Steam hosting,
visibility changes and invite-overlay activation have been exercised live on
build 19221447 under Proton. LAN discovery/join selection, local Continue
from Start Game, the shifted title Quit to Desktop row, restoring vanilla
pause labels, both vanilla pause quit confirmations, and visible gameplay connect/disconnect events against the
simulated peer were also verified. Two Steam accounts on two machines (a Proton desktop and a Steam Deck) were
used to verify: friends-only hosting from the title and pause menus, JOIN A
FRIEND from the title and pause menus, public visibility with FIND PUBLIC
GAMES, START GAME into shared gameplay over SteamNetworkingMessages, guest
LEAVE SESSION, host CLOSE SESSION, and rejoining mid-game. LAN hosting from
the pause menu and JOIN LAN GAME were verified between the same machines.
If the original Steam host leaves, the session ends instead of migrating
campaign authority. Steam invite acceptance (`GameLobbyJoinRequested` /
`+connect_lobby`), controller navigation and every mouse target still
require acceptance testing. There
is no synchronized ready/start handshake or host-authoritative mission
replication yet. Save transfer is optional and not required to open a lobby.

## Mission and save discovery

Set `MissionDiscovery=1` in the INI and restart to trace mission-, objective-,
checkpoint-, save-game-, load-game-, and player-profile-related Unreal
`ProcessEvent` calls. The log records each unique class/function signature once,
including parameter names, reflected types, offsets, and sizes. It does not log
parameter values, suppress calls, block saves, or change game state. Disable it
after collecting a bounded log; tracing is for identifying runtime call
signatures, not proof that a call safely controls campaign loading or saving.

The trace is deliberately diagnostic. Do not test guest save suppression or
join behavior on a valuable campaign until the relevant lifecycle has been
verified and the save files are backed up. Offline strings and function names
are leads only; they do not establish call order, ownership, or side effects.

### Save and load lifecycle (verified on build 19221447)

Observed with `MissionDiscovery=1` and file timestamps under Proton:

- The main menu is drawn over an already-loaded `ORWorld`. `BP_MissionManager`
  and mission conditions run there, and the game writes `checkpoint.sav`
  (`OnGameSaved` type 0) and `profile.sav` while the menu is idle.
- **Continue** does not travel to another level. It fades to black,
  broadcasts `ReceivePreSaveGameLoaded(bIsCheckpoint)` to a few hundred actors,
  loads in place (about 7 s), then broadcasts `ReceivePostSaveGameLoaded`.
- Right after a load, the game writes `profile.sav` and an `autosaveNN.sav`
  (`OnGameSaved` type 5). The R quicksave writes to the same autosave rotation.
- The native load and save calls are not visible to `ProcessEvent`; only the
  Blueprint events around them are.
- Saves are written directly with `CreateFileW` (no temporary file and rename)
  below `Saved\<SteamID64>\SaveGames`.
- A save from another Steam account loads normally. Saves are not tied to the
  account that wrote them.

`GameMode.SetSavingBlockedByBlueprint(true)` is **not** a save boundary.
With the flag held from controller creation onward, the game still wrote
`checkpoint.sav` and `profile.sav` at the main menu. Debug and passenger
sessions still set the flag, but only to reduce mid-session autosaves.

### Guest save sandbox (`HostSave`)

With `HostSave=1` (the default) the plugin patches the game executable's
KERNEL32 imports for file creation, deletion, renaming (`MoveFileW` and
rename-by-handle), attributes and directories (`src/coop/save_guard.cpp`) at
startup, in **passthrough** mode: nothing is redirected while you play alone or
host. The moment this player joins someone else's session, the guard switches
on and redirects every write below a `Saved\...\SaveGames` folder into
`TheysGone-guest-saves` next to the plugin. That folder is wiped at each
join.

- Writes are copy-on-write: modifying an existing save first copies the
  original into the sandbox.
- Reads prefer the sandbox copy and fall back to the real file.
- Deletes and moves never touch a real save; deleted names are hidden for the
  rest of the session.
- The path rules are pure functions in `src/coop/save_policy.hpp`, covered by
  `make test`.

Verified live: across menu idle, Continue, the post-load autosave and a manual
quicksave, every write went to the sandbox. The SHA-256 of every real save was
unchanged afterwards. Copying another account's saves into the sandbox at the
main menu, then pressing Continue, loaded that account's progress. That is the
basis for a host-to-guest save transfer.

Known limits:

- Until a host save is adopted, wildcard directory listings (the **Load Game**
  list) still show only the real folder.
- Only the executable's own imports are hooked.
- While joined, **nothing you do in that session is kept**. Set `HostSave=0`
  to play your own save instead.

Back up `SaveGames` before experimenting anyway.

### Host-to-guest save transfer (`HostSave`)

The guest starts from a copy of the host's save:

1. A connected guest sends a `DGSV` request every second.
2. The host picks the `%LOCALAPPDATA%\BendGame\Saved\<account>\SaveGames`
   folder with the newest `profile.sav`. It reads every `*.sav` file (retrying
   if a file changes while being read) into one bundle: a file table with
   names, sizes and per-file SHA-256, then the contents. A snapshot is reused
   for 30 s and dropped after 5 idle minutes.
3. The host answers with an offer (transfer id, size, bundle SHA-256). The guest
   pulls 1 KB chunks by index, 24 per tick, and pulls missing ones again after
   0.5 s. Nothing is pushed unasked, so loss and reordering only cost retries.
4. The guest checks the bundle digest and every file digest and name, then
   adopts it: the sandbox is replaced with the host's files and the guard
   switches to **exclusive** mode. Reads no longer fall back to the real
   folder, and wildcard listings show the sandbox.
5. Adopted files are **pinned** until a load that started after adoption
   completes (`ReceivePreSaveGameLoaded` ... `ReceivePostSaveGameLoaded`).
   Adoption waits while a load is already running. While pinned, game writes to them go to
   `TheysGone-guest-saves-discard` and deletes are ignored, so the menu's
   own `profile.sav`/`checkpoint.sav` writes can't replace the host's files
   before Continue.
6. A guest already in gameplay reloads in place with
   `BendGameMission.ReloadBecauseOfJobFailure`; at the title menu, Continue
   loads it. After the load the pins are released, the guest's autosaves go
   to the sandbox, and the guest is teleported to the host once both are in
   game.
   While host saves are pinned, the log records `.sav` opens with the
   sandbox path, access, disposition, and open result (up to 128 per
   adoption). `ReloadBecauseOfJobFailure` may return false even when its
   asynchronous load later succeeds; use the verified story signature as the
   result.

All datagrams carry the sender's session id and are ignored unless it matches
the connected partner. File names are limited to `[A-Za-z0-9_.-]` ending in
`.sav` (reserved device names such as `nul` or `com1` are refused), at most
32 files and 64 MB.

Verified live on one PC (build 19221447, Proton) with `fake_peer.py`:

- Guest: 7.3 MB of another account's saves arrived at the title screen in about
  13 s without retransmits and were adopted. The menu's `profile.sav` write
  went to the discard folder. Continue loaded the host save (the post-load
  autosave matched that account's save size, not the local one), the guest
  teleported to the partner, and all 20 real save files kept their SHA-256.
- Host: the plugin found the right account folder and served a 7.4 MB bundle
  in about 10 s. Every fetched file matched the snapshot byte for byte.

Since then verified between two real PCs over the LAN transport (Proton,
desktop Linux and Steam Deck), including the in-place reload. Not yet tested
over the Steam transport. The host shares its last save on disk.

### Story sync (`DMST`)

Each side samples a signature of `BendJobManager`'s jobs and missions every
3 s, including each job's objective index and every current objective ID/state
in active jobs
([`story.hpp`](../src/coop/story.hpp)). The host sends its live signature, the
signature at its last save, a save serial and a monotonically sequenced packet
every second; the guest ignores duplicate or stale packets. When a guest's
signature differs from the host's saved one for 6 s, and the host has saved
this session, the guest requests the host's save again and reloads onto it.
This syncs persisted progress through the save-resync fallback. Story packets
are version 3; both peers need a build with this version. With
`MissionObjectiveSync=1` on both peers and `HostSave=1`, the host also retries
uniquely identified completed objectives in sequenced `DMOB` packets every 3 s.
The guest applies those idempotently only after adopting the host save and
entering gameplay. Job start/completion, ambiguous IDs and failed applications
still rely on save resync. This opt-in path has a paired LAN validation for one
completed objective; the isolated host's sends were also observed with
`fake_peer.py`. The Deck logged `objective sync: applied IND-JobData.IND3 from
host` and its story signature matched the host. Save-resync fallback was also
verified with DMOB disabled, including after a host objective autosave. Job
start and job completion were validated on a paired LAN run: neither travels
in DMOB, and both reach the guest through save resync at the host's next save.
Starting a job does not autosave by itself. Completing one autosaves only after
the host dismisses the MISSION COMPLETE reward screens, which block saving
(`IsSafeForOWSave` reason 12). The guest then matched within about 33 s.
A paired DMST v3 run also verified the saved `IND4` state-0-to-1 transition:
the guest fetched the changed host save, reloaded automatically and matched
both the objective state and story signature. This covers one non-completed
objective transition, not broader campaign progression. DMOB continues to
send only completed (state 2) objectives, so the option stays default-off.

## Shared enemies

With *Sync host's enemies* on, both PCs set `kSyncEnemies`:

- **Host.** Streams pose and appearance (`DPOS`, `DMEN`) for at most 16 hostile
  AI pawns within 80 m of either player. Hostile means a freaker or animal
  controller, or `BendAIController.IsEnemy(player)`; camp NPCs and companions
  stay local on each PC.
- **Guest suppression.** A joined guest with `HostSave=1` stops its own AI:
  `BendAmbientGenerator.OverrideGlobalSpawnLimitNEW(0)` and `ForceDespawnAll`,
  `BendAmbientSpawnArea.SetAreaActive(false)`, `BendCrowdGenerator.SetPaused`,
  and every other hostile pawn is returned to its pool with the generator's
  `DespawnUnit` (or `BendAIController.Despawn`). `K2_DestroyActor` does not work
  on pooled pawns: they come back. Pooled, unused pawns
  (`bPooledSpawn && !bPooledSpawnInUse`, native bitfield bools at
  `UBoolProperty`+0x78) are skipped. Everything is restored on leave.
- **Guest to host (`DMEH`).** A guest shot is tested along the camera ray
  against each replica's body; a melee hit must be within 4 m and in front.
  Both need a clear world trace. The weapon's damage goes to the host, which
  applies it with `ApplyDamage` to the real enemy, instigated by the host's
  controller. Hits are sent twice and deduplicated.
- **Host enemies to guest (`DMPX`).** Host AI only fights pawns, so the host
  spawns an invisible, brain-stopped `SwarmerPawn_C` with the player's faction
  that follows the guest's avatar. Every 0.5 s, enemies nearer to it than to
  the host get `SetAwarenessOfPawn` and `SetFactionValueOfPawn` for it, so
  they pick it as a target. During stealth missions
  (`kMissionStealth`) only enemies that already have a target get them, so
  the stand-in cannot alert a camp nobody has been seen by. Freaker hit tests only damage the player, so the
  host watches `AnimNotify_MeleeArmed_C.Received_NotifyBegin` through the
  ProcessEvent hook: a swing from an enemy targeting the stand-in within
  2.6 m sends a strike (swarmer 12, others 15-35, an estimate). Real damage
  the stand-in takes is forwarded too, capped at 400 per change. The guest
  applies strikes with `ApplyDamage`.
- **Guest-kept enemies to host (protocol v9).** While the guest keeps enemies
  through a reunion, it creates the same hidden proxy at the host's avatar.
  Nearby guest-owned enemies can target it; their swings and damage use the
  same `DMPX` path, now accepted by either player owner. This direction still
  needs paired validation.

Verified live between two PCs: the guest's own enemies vanished and the host's
appeared; `hit-enemy` brought a host swarmer from 160 to below 0; a pack split
its aggro between the host and the stand-in and its swings killed the guest.
Real aimed shots and melee from the guest, and ranged enemies, are not yet
verified.

### Reverse-engineering findings and failed approaches

These findings came from live inspection on build 19221447 under Proton,
using the reflection dump, object census and ProcessEvent trace described
above. They are observations for this build, not a recovered Unreal
multiplayer implementation.

1. **Separate hostile AI from camp NPCs.** Inspecting pawn controllers exposed
   `BendFreakerController`, `BendAnimalController` and `IsEnemy`. The shared
   enemy scan uses those checks rather than copying every `BendAIPawn`.
2. **Return pooled AI instead of destroying it.** `K2_DestroyActor` did not
   keep the guest's enemies gone. Controller inspection exposed
   `bPooledSpawn` and `bPooledSpawnInUse`; their native bitfield masks are
   read from `UBoolProperty` metadata at property offset `0x78`. Returning
   active pawns with `DespawnUnit` and pausing generators stopped the local
   duplicates.
3. **Give host AI a real target.** The rendered partner is a non-colliding
   skeletal actor, not a pawn. A spawned `SwarmerPawn_C`, with its brain
   stopped and faction set to the player's, supplies a target. Merely setting
   the faction was insufficient: `IsEnemy` reported hostility, but enemies
   still chose the host. Calling `SetAwarenessOfPawn(1)` and
   `SetFactionValueOfPawn(1)` every 0.5 s for streamed enemies within 40 m
   and closer to the stand-in than the host made the pack split targets.
4. **Wait for health initialization.** Giving the stand-in 5,000 health
   produced phantom damage when the engine subsequently initialized it.
   The implementation now waits 1.5 s, captures its settled health (160 or
   200 observed), forwards subsequent drops of at most 400 and refills to
   that baseline. Larger drops are logged and not forwarded.
5. **Observe swings when native collision does not deliver damage.**
   Freakers surrounded the stand-in and swung from as close as 68 cm, but
   its health remained unchanged with damage enabled. Tracing revealed
   `AnimNotify_MeleeArmed_C.Received_NotifyBegin`,
   `AnimState_AIMeleeAttack_C` and
   `AttackMgr_Swarmer_C.OnAttackerReachedSlot`. The implementation hooks the
   first event, resolves its mesh parameter's owning pawn, checks that the
   controller targets the stand-in and checks the 2.6 m range. It forwards
   estimated damage at notify time; this is not proof of a native hit.

Current base damage estimates are 12 for swarmers/newts, 35 for
breakers/reachers, 20 for wolves/ragers and 15 otherwise. These are not
measurements of the game's attack damage. Only the swarmer path has been
verified live; other classes still need their notify and damage behavior
checked.

### Combat delivery and authority

[`enemy.hpp`](../src/coop/enemy.hpp) defines a 24-byte `DMEH` hit packet
(session, entity, sequence, attack kind and damage) and a 20-byte `DMPX`
strike packet (session, sequence and damage). Both have their own version,
reserved fields and positive damage limit of 1,000. Receivers validate the
packet and current partner session; a 64-entry sequence window suppresses
duplicates. Each hit is sent immediately and once again after 0.1 s.
There is no acknowledgement or retry beyond that second send, so losing
both copies loses the hit.

The guest selects hits against approximate bodies, not native enemy hitboxes:
a 50 cm-radius standing body for shots, or range and facing checks for melee,
with a world obstruction trace. Weapon damage is the midpoint of the native
weapon damage range. The host checks the entity is still alive and in range
(150 m for shots; 8 m for melee, including latency allowance), then calls
`ApplyDamage`. The host does not independently reconstruct the guest's aim
or weapon damage; this remains a trusted-partner protocol. The game can
modify base damage: a 25-point debug hit reduced one swarmer by 18.75.

### Current validation and remaining work

| Surface | Evidence / status |
|---|---|
| Host save and sandbox | LAN transfer and in-place reload verified between two real PCs; real guest save hashes remained unchanged in the sandbox tests |
| Story catch-up | Forced divergence and a host objective autosave both triggered host-save resync with DMOB disabled; the Deck returned to the host signature. Natural campaign progression remains unverified |
| Guest AI suppression | Local enemies disappeared and host replicas appeared in the two-PC LAN test |
| Guest damage to host enemies | `hit-enemy` damaged and killed a real host swarmer; this bypasses aim and does not validate real shooting or melee input |
| Host melee damage to guest | Swarmer pack targeted the stand-in, damaged and killed the guest; guest and stand-in respawned, and a later clean-build strike reduced health from 121.9 to 109.9 |
| Free roam enemy handoff | Paired `move-by` tests: the guest kept 8 Swarmers across a reunion and the host's hits applied to them; the host kept 6 Swarmers fighting the guest across a split, which kept striking the guest and took guest hits 312 m from the host. Debug-spawned packs only; not yet verified in a natural mission fight |
| Native bike interaction | Standalone `ride-test` prompt exercised; boarding a real partner's bike through it still needs a player test |
| Steam transport | Earlier lobby/transport tests do not validate these new gameplay paths; current gameplay evidence is from UDP LAN |

Complete host-first synchronization is **not finished**. Live mission state,
inventory, loot, ranged enemy damage and shared bike simulation remain outside
the implemented paths. Swing forwarding has no native contact, obstruction,
dodge or attack-outcome confirmation; proxy-health forwarding has no damage
source attribution. Check for double delivery if an attack both triggers the
notify and damages the proxy. The stand-in's capsule blocking the host and
host shots accidentally hurting the guest through it remain untested.
Disconnect cleanup resumes generators, but does not snapshot and restore
their individual pre-session pause/active states.

During Deck testing, the native CO-OP history displayed disconnect/leave
messages without corresponding transport log entries while the UDP session
remained connected. The cause is unresolved; do not treat those menu messages
alone as proof of a transport disconnect.

## Offline executable analysis

```sh
make inspect EXE="/path/to/DaysGone.exe" \
  MANIFEST="/path/to/steamapps/appmanifest_1259420.acf" > engine-inspection.json
```

This is read-only. It requires the executable's SHA-256 and the manifest's
build ID to match [`config/steam-19221447.json`](../config/steam-19221447.json),
then validates the PE structure. It reports reflection-string anchors and the
functions that reference them. The results are leads for analysis, not proven
engine addresses. The tool never attaches to a process or writes anything
except its report.

## Save files

The save format research and the save editor (Python codec/CLI and browser
editor) are maintained in the separate
[days-gone-save-editor](https://github.com/drskunk/days-gone-save-editor)
repository. The transfer code in `src/coop/` treats saves as opaque bytes.

## Releases

Releases are automated with
[release-please](https://github.com/googleapis/release-please-action) and
[Conventional Commits](https://www.conventionalcommits.org/):

| Commit prefix | Effect (before 1.0) |
|---|---|
| `fix:` | patch release |
| `feat:` | minor release |
| `feat!:` / `BREAKING CHANGE:` | minor release (protocol-incompatible) |
| `docs:`, `ci:`, `build:`, `test:`, `chore:` | no release on their own |

1. Merge to `main`. The **Release** workflow opens or updates a release PR
   that bumps `version.txt`, `pyproject.toml` and `CHANGELOG.md`.
2. Merge the release PR. release-please tags `vX.Y.Z` and creates the GitHub
   release.
3. The same workflow builds the ASI with that version, packages
   `TheysGone-X.Y.Z.zip` with a SHA-256 file, creates a signed build
   provenance attestation and uploads both files to the release.
4. After the GitHub assets are uploaded, the reusable **Nexus Mods** workflow
   downloads that exact archive, verifies its checksum and GitHub attestation,
   and uploads it to [Nexus mod 1019](https://www.nexusmods.com/daysgone/mods/1019).
   It updates the mod version and changelog, keeps older versions available and
   disables mod-manager downloads because Vortex installation is not supported.
   A Nexus failure is reported without removing the GitHub release.

The **CI** workflow runs the tests and uploads a development build as a
workflow artifact on every push and pull request. Dependabot keeps the pinned
GitHub Actions and Python dependencies current.

Release PRs are opened with the default `GITHUB_TOKEN`, so they don't trigger
the CI workflow themselves. Their changes are limited to version and changelog
files.

### Nexus publishing setup and recovery

Configure these in the personal repository's **Settings > Secrets and
variables > Actions**:

- Secret `NEXUSMODS_API_KEY`: a Nexus API key belonging to an account with
  upload permission for the mod. Never put the key in source control or chat.
- Variable `NEXUSMODS_FILE_ID`: `8193009`, the existing file's **API File ID**,
  not its short unique ID.
- Variable `NEXUSMODS_MOD_ID`: `16333760627707`, the **Unique Mod ID**, not the
  page's game-local number `1019`.

The initial mod page and first file must be created on Nexus manually.
Subsequent stable releases publish automatically; missing credentials fail
explicitly. Nexus may still scan or moderate the file before it is downloadable.

For a non-publishing check of an existing release:

```sh
gh workflow run nexus.yml -f tag=v0.2.0 -F publish=false
```

To upload an existing GitHub release after a failed Nexus job, run **Nexus
Mods** from the Actions tab with its release tag and **Upload to Nexus**
checked (or use `-F publish=true`). Check Nexus first: uploading is not
idempotent, so do not rerun an upload that already created the version.

The mod page text lives in `docs/nexus-description.bbcode`. After editing it,
push it with the **Nexus Mods page** workflow (`gh workflow run nexus-page.yml`),
which sets the summary and description through the experimental v3
`PATCH /mods/{id}` endpoint. The API cannot change the mod name, images or
tags; edit those on the website.
