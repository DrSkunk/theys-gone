# Engine replication: handoff

Handoff for continuing the built-in Unreal replication investigation in
another tool or session. Written 2026-10-22, updated 2026-10-25. Read this
file first, then [REPLICATION-RESEARCH.md](REPLICATION-RESEARCH.md), which
holds the full evidence, the Phase 1 log and the test plan.

## Prompt to paste

```text
You are continuing a reverse-engineering investigation in the Theys Gone repo
(a two-player co-op ASI mod for Days Gone, Steam build 19221447). Goal set by
the user: full engine replication with the guest
as a thin client, because only that gives a seamless experience. The current
shipped mod uses its own datagram layer plus a host save transfer.

Read, in this order:
1. docs/REPLICATION-HANDOFF.md (state, environment, gotchas, code map)
2. docs/REPLICATION-RESEARCH.md (evidence, Phase 1 log, the two-run test plan)
3. src/coop/research.cpp (all experiment code: net-probe, net-listen,
   net-connect, net-possess, net-takeover, hooks)

State: the host dump's runtime metadata identifies `UE4 BendGame
4.11.1-0+UE4`; earlier documentation assumed 4.10.1 from source-pattern
comparisons. All offsets are specific to build 19221447. SteamNetDriver
connects two real PCs and replicates gameplay actors. Run F removed the
guest navigation crash. Run G (G1–G21) made mouse/keyboard thin-client
movement and look work. The causes:
- Bend's character classes have empty `GetLifetimeReplicatedProps`;
  `PawnReplication=1` substitutes stock `ACharacter` registration, restoring
  engine-base fields. Run L directly proved it omits Health (RepIndex 22).
  `BendHealthReplication=1` additionally registers Health; Run M verified
  two post-spawn host damage updates reaching the guest.
- A replicated PC never runs Bend `Possess` (`ClientPossess=1`).
- Root-motion moves are rejected by the server
  (`ClientAuthoritativePosition=1`).
- One static input stack is shared by both local PCs.
- Bend's camera only takes mouse input for player 0 (`net-takeover` now
  promotes the guest PC to index 0).
The guest walks and turns with mouse and keyboard, and the host sees the
same position. Gamepad does not reach the guest yet. Run H's host-spawned
swarmer moved and took damage, but the host crashed in
`ABendAmbientGenerator::OnPlayerRespawn` while binding its
`OnPlayerTeleported` delegate. Offline disassembly resolves the null
`+0x8d8` field to `BendPlayerController.MyBendPawn`. Run I then left the
joined controller unpossessed for 2 minutes 27 seconds without a crash,
assigned it a matching Bend pawn, and verified host invulnerability with a
native damage probe. Run J added per-actor swarmer telemetry and identified
the host-spawned actor as `SwarmerPawn_C_30` and its guest simulated proxy as
`SwarmerPawn_C_60`. Their paired locations matched within about 0.5 cm, but
after host damage the authoritative health was 156.25 while guest health
remained 200. This proves movement replication for the test actor, not
health, damage, death, or full AI-state replication. Run I still does not
prove the Run H crash cause. Run K found bReplicateHealthToAll already true;
it did not fix stale Health. Run L observed 17 stock lifetime entries on
both PCs, none at Health's RepIndex 22. Run M with PawnReplication=1 and
BendHealthReplication=1 added only 22:COND_None; guest enemy Health matched
the host after two separate hits (156.25, then 152.5), and OnRep_Health ran
three times. Its native virtual implementation is empty, as is
OnRep_LastTakeHitInfo, so this does not restore damage/death presentation.
Both PCs' originals were restored and verified between runs; current away-
session backups are ~/TheysGone-research-backups/20261024T1525Z-runK-pre.
Run N exposed missing guest mesh/animation and a grapple guard that
clamped lethal damage to Health=1. Run O killed a fresh owned enemy before
grappling: both peers reached Health=-320, IsAlive=false, bTearOff=true.
Only the host had bIsDying=true; the native guest pawn proxy had no mesh or
animation and survived host cleanup. These Runs N/O used `Transport=menu`;
custom co-op `DMEN`/`DPOS` traffic was zero, so they did not exercise the
production visual enemy replicas. That code already sends host mesh paths
and evaluated poses, gates guest hits on alive state, and expires stale
replicas. The two-client QA of that path has since passed (see "Where things
stand", 2026-10-25 18:03); remaining work is mission start/complete sync,
interactions/pickups, enemy death visuals and long passenger rides. Run 1 had production guest replica creation and nonzero pose application:
three Crow meshes and a Leon mesh (475 bones, three parts) were instantiated,
with measured application rates of 38.9/s and 118.9/s and zero rejected poses.
That run was telemetry only; the later live-save fixture run covers hit/death.
The Deck is now in KDE Desktop Mode (Wayland, DISPLAY=:0,
WAYLAND_DISPLAY=wayland-0). Its startup blocker is operationally resolved:
complete Initial Setup without the ASI in the isolated prefix, then run the
restored mod under Proton 11's required installed Steam Linux Runtime 4.
The user selected Limited Data at the game's first-run prompt. The restored
mod now shows a real populated menu with NEW GAME first and CO-OP inserted;
ui-probe confirms five options and an active main page. The isolated launcher
persistently enters Runtime 4, with its previous script backed up. The
successful tests are stopped. The remote helper's gamescope-0 environment is
stale: use Spectacle through the user systemd manager for desktop captures.
Both isolated launchers now use Runtime 4. A reproduced production join bug
remains: a guest already on NEW GAME does not gain Continue after adoption.
RecheckSaves() changes bSaveExists to true, and HasAnySaves() returns true,
but even a native redraw leaves the five-row NEW GAME menu unchanged.
Those ineffective candidate changes were removed and both PCs restored to
ASI 6315e1de.... Only the diagnostic save-query extension remains in source.
An isolated save-preload workaround did load a guest pawn and enable the
production-stream observations, but is not a seamless-joining fix.
(Both were later resolved: late-adoption join fixed and a live-save fixture used.)
Do not call continue-title on NEW GAME. Do not access live installs or saves. Both peers must
have identical lifetime flags before connecting; restart to change them.

Rules: build with `make build`, test with `make test` (Poetry project, never
create a venv). Do not use or modify the owner's live game installs or saves.
Use isolated test copies and back up files before changing them; ask before
copying/downloading large installs, installing software, writing outside the
workspace, or using accounts. The remote shells may be fish: wrap remote
commands in bash -c '...'. Do not contribute or publish unless asked. Record
every finding in docs/REPLICATION-RESEARCH.md as you go.
```

## Where things stand

**Menu timing implementation (2026-10-31, protocol v11):** the connected
weapon/crafting wheel removes only its own dilation request. Native pause,
map and inventory now carry a `kMenuPause` vote: gameplay pauses only while
both players have a menu open, and resumes when either closes. Disconnect
restores single-player behavior. This is not a blanket reset of focus/combat
or scripted pauses. See the timing section in REPLICATION-RESEARCH.md for
the candidate's paired-test evidence and remaining coverage.

**Free roam implemented and paired-validated (2026-10-27):** with
`FreeRoam=1` on both peers, the host's native dual-focus streaming hook keeps
the guest's region loaded. When the players are more than 250 m apart, the
host sets state flag `kSeparated`, and each game owns the
enemies around its own player. The guest also streams enemy appearances and
poses to the host, which renders poseable replicas while the guest remains
authoritative for their AI and health. Under 150 m, the host owns new enemies
again.

**Gradual enemy ownership handoff (protocol v9):** each enemy has one owner,
and hits go to the owner in either direction. On a split or reunion, enemies
that were fighting the guest in the last 6 s stay with their current owner and
keep streaming until they die, lose interest for 6 s, or reach 180 s. The
guest no longer despawns a fight in progress when the players regroup.
Protocol v8 handoff passed paired validation; protocol v9 adds a guest-side
host proxy so guest-kept enemies can target and damage the host, pending a
paired test.

Validated on Cachy and the Deck:

- separation;
- guest-region AI;
- host rendering guest-region wildlife and a Swarmer replica (with
  continuous pose updates and no packet rejects);
- guest death, respawn and reconnect;
- reunion handover;
- host hits on guest-owned enemies, and the guest keeping 8 Swarmers across
  a reunion;
- the host keeping 6 Swarmers that were fighting the guest across a split,
  with their swings still reaching the guest and guest hits landing 312 m
  from the host.
- a final-build protocol-v8 regroup retest: the guest kept all 8 active
  Swarmers (2 uninvolved sources were not kept), then released them on
  cleanup. The guest rejected a host hit at 292 m as out of range.

Open issues:

- The protocol-v9 guest-side host proxy is implemented but not yet paired-
  validated for enemy retargeting or damage to the host.
- Enemy identities are not carried over once an enemy is released to the
  new owner.
- Interactions in the guest's region are not host-authoritative.

Details are in `REPLICATION-RESEARCH.md`, "Free roam: dual-focus streaming
and regional enemy authority". Test with `move-by` in steps of a few hundred
metres; a single kilometre-scale jump can land in unloaded terrain.

**Independent-roaming engine work (2026-10-27):** the user's target is two
real engine participants who can roam separately, with targeted native
reverse engineering where reflection is insufficient. Prioritize the
world-streaming, simulation and player-registration foundations rather than
treating nearby replica QA as proof of full co-op. The production host's guest
stand-in is an AI pawn; selecting existing enemies near either player does
not establish that distant guest regions are loaded or simulated.

Read-only analysis of the hash-verified isolated executable traced
`SetStreamingPosition` from native thunk `0x13c8750` to `0x5bdd70`, then
`World.WorldComposition` to `0x2c65b10` / `0x2c65b30` and tile selection at
`0x2c60250`. The inspected update body selects from one position even though
the normal caller supplies a viewpoint array and count. Do not repeatedly
replace the world's streaming position with each player's position: that
overwrites shared state rather than establishing a union of required tiles.
That trace led to the opt-in, build-guarded dual-focus hook and paired
regional-enemy tests summarized above. Ambient spawning works independently
on the guest, and the host renders the guest's streamed enemy poses. Enemy
combat authority remains local to the guest while separated; navigation
parity and host-side interaction with those enemies remain unverified. Full
evidence is in the research log's "Free roam: dual-focus streaming and
regional enemy authority" section.

**Latest paired resync attempt (2026-10-27, 09:41 CET): BLOCKED before an
objective action.** Cachy loaded the `fa97ef49` fixture. The Deck connected
over UDP and installed the host snapshot, but its runtime main menu and
visible Continue row never appeared, so no resync trial was run. Both
isolated setups were stopped and restored from fresh backups. A guest-only
follow-up on the correct display with the configured input helper still did
not produce a runtime menu. Details and evidence are in
`REPLICATION-RESEARCH.md` under “Repeated paired resync attempt blocked at
guest startup.”

**Late-adoption join fixed (2026-10-25, 17:29 CET):** the guest's title rows
are built by the menu's own `WaitForInput(bForced)` ("press any key") step,
not by `RecheckSaves` or a class default. Once a host save is adopted under
an already-open menu, `native_ui.cpp` (`MainRebuildForSaves`) re-runs that
step with `bForced=true` and re-adds one CO-OP row. In the exact failing
order, the guest went from 5 rows (NEW GAME first) to 7 rows (CONTINUE first,
one CO-OP) 130 ms after adoption. The guarded Continue then loaded the host
save into a health-135 pawn, with no save-preload workaround. Both isolated
copies run ASI `e84995de…` (the previous one is kept as
`TheysGone.asi.6315e1de`), and both test units are stopped. The guest's real
isolated SaveGames is unchanged.

**Live-save fixture QA passed (2026-10-25, 18:03 CET):** with the live host
saves copied into the isolated host (backups in `run1/`), the production UDP
path passed these checks:
- Join.
- Enemy spawn and replica.
- Enemy → guest damage and guest → host-enemy damage.
- Host kill, with a dead replica that can't be hit.
- Replica cleanup.
- Matching story signatures, plus automatic resync after a forced guest
  divergence (~26 s).
- Guest disconnect and reconnect against a running host.

Still open:
- The visual fidelity of enemy death on the guest.
- Paired validation of save-backed objective sync and direct mission
  start/complete replication.
- Interactions and pickups.

The eight-enemy pack replicated and cleaned up correctly; details and the
inconclusive death screenshot check are in `REPLICATION-RESEARCH.md`.

**Mission tracing:** read-only discovery found `IND` checkpoint/objective
functions executing, but the story signature stayed unchanged. The 2026-10-26
mission lab (`mission-*` commands) then drove a real objective transition
live: `IND_Jobscript_C.MedBoxObtained` moved `IND-JobData` from objective 5 to
6 and the HUD switched to "Take sterile bandages to Boozer". The chain runs
inside Blueprint code, so ProcessEvent hooks see nothing; poll
`CurrentJobObjectiveIndex` instead. `Debug*` job functions are no-ops in this
build. `CompleteTheJob` did work on the isolated fixture and changed IND to
complete, moving tracking to the next job. The story signature includes job
objective indices and active objective identifiers/states; version-3 story
packets reject stale sequence numbers. Both peers need the same protocol
version. A paired v3 run verified the saved `IND4` state-0-to-1 transition
through automatic guest save resync, matching signature `1e6f784a`.
Opt-in direct DMOB application remains limited to uniquely identified
completed objectives; broader campaign progression remains untested. Paired
objective-resync validation is documented in
`REPLICATION-RESEARCH.md`. A nearby bike
interaction attempt and safe native-prompt probe were inconclusive at first.
The safe `ride-test` prompt was confirmed in range: E logged both
`interaction completed` and `test interaction completed`. In the later
production test, the guest initially pressed E before overlapping the bike
prompt; after moving into the interaction volume, E fired
`OnInputSucceeded` and the guest boarded via `SOCKET_PassengerSeat`. A brief
host ride showed the guest staying seated and tracking the bike on both
screens. The `CanRecieveFocus=false` and zero interact-location samples also
appeared near successful interaction, so they are not conclusive on their own.
Production passenger boarding and short-distance follow are verified; live
mission start/complete sync, broader interactions/pickups, and long-distance
passenger stability remain open. See the latest passenger section in
`REPLICATION-RESEARCH.md`.

Both isolated units are stopped. The guest's real isolated SaveGames still
match the pre-bootstrap backup. The host's post-run saves were preserved and
the known on-foot fixture restored. Both INIs retain `MissionDiscovery=0`;
final logs and traces are archived under `run1/logs`.

The historical notes below predate the latest passenger test.

**Latest paired outcome (2026-10-25, 16:55 CET):** both isolated games loaded
health-135 pawns after production save transfer, using a backed-up,
isolated-only guest save-preload workaround. The guest created four production
enemy replicas and applied poses with zero rejected packets in the observed
samples. This is not a complete enemy QA pass: the opening mission starts on
a bike and the in-game controlled fixture correctly refused it.

The fresh-guest bootstrap failure is now precisely reproduced: the guest
menu starts NEW GAME first with five rows, then receives four host files.
`RecheckSaves()` alone, and with `UpdateSelectedPageOptions`/`MainLabelUpdate`,
leave that already-open menu unchanged. In the redraw test, `bSaveExists`
and a fresh `HasAnySaves()` query both became true. The missing piece is menu
reconstruction, not download or save availability. A misleading early-adoption
test did show Continue/seven rows and loaded a pawn, but the exact late-
adoption regression failed; do not cite the early test as a fix.

Both experimental test pairs are stopped. Both isolated installed ASIs were
restored and hash-verified as
`6315e1de8a3fae210f9d2ca3ea253f86e88b2602e9d820ee9901bb798e8b17bb`.
Rejected production refresh code was removed; `ui-probe` now additionally
logs cached and freshly queried save availability. The original fresh guest
SaveGames was restored; the preloaded version and transferred bundle remain
separate isolated backups. Live installs/saves were untouched. Next: trace
the native menu reconstruction path, keeping Continue guarded, and prepare
a safe on-foot/non-mission bot fixture rather than bypassing QA guards.

**Earlier Deck startup outcome (2026-10-25, 16:25 CET):** completing the
isolated prefix's first-run setup in the no-ASI baseline, then restarting the
restored mod under Steam Linux Runtime 4, produced a real title splash and
populated main menu. The user approved Limited Data. `ui-probe` reported five
options, `mainPageActive=true`, and firstLabel=NEW GAME; the log and screenshot
confirmed the added CO-OP row. No gameplay pawn exists yet. The successful
test unit is stopped. The initialized prefix is backed up as
`run1/Saved.initial-setup-complete-20261025`.

The isolated `launch-deck-test.sh` now enters the installed Runtime 4 before
Proton 11; its original is saved as
`launch-deck-test.sh.before-runtime4-20261025`. Local/remote `bash -n` passed.
Launcher SHA-256:
`dc3a9cf42f9676303ed4b5e2c587ca4e56f41c74462bb503f05250c6e4a96a53`.
The launcher smoke test subsequently passed, the isolated host was started,
and production transfer/bootstrap testing proceeded as described above.
The exact first-run save-guard interaction is not yet proven;
do not implement a speculative production fix.

**Previous Desktop Mode diagnosis (2026-10-25):** the user switched the
Deck to KDE Wayland. Its user-manager environment is now `DISPLAY=:0` and
`WAYLAND_DISPLAY=wayland-0`; no Gamescope compositor was running. Spectacle
captured the desktop correctly before launch, then a black fullscreen game
surface during the isolated test. The existing remote helper still has
`WAYLAND_DISPLAY=gamescope-0` and is not a valid desktop capture source.

The first desktop run used the existing launcher unchanged. Its journal
reported missing `libvpx.so.9`, `libtheoradec.so.1`, and `libnettle.so.8` while
loading Proton GStreamer plugins. Proton 11's manifest requires AppID
4183110, verified locally as the installed Steam Linux Runtime 4. A second
run wrapped the unchanged launcher in that runtime's `_v2-entry-point`.
Those multimedia warnings disappeared, and a desktop capture briefly showed
the game's loading icon. However, a later capture was black again, and
`ui-probe` at 15:34:26 still found a visible main-menu widget with zero
options, no active main page, no focus, and only the title-widget template.
`status` still reported `active=0 playerHealth=0 owned=0`.

Both variants also logged a failed copy of `amdxcffx64.dll`; its significance
is unknown. No dependencies were installed and the launcher was not edited.
The user then authorized an isolated no-ASI comparison. With the same runtime
and prefix, that run displayed Initial Setup after about 100 seconds. No
input was sent; a later capture had mostly faded labels. This is not yet
proof of a working main menu, but it directs the investigation toward mod
startup and first-run profile handling. The isolated prefix Saved tree was
backed up as `run1/Saved.before-no-asi-20261025`, and the ASI was restored and
hash-verified after the run. All desktop test units are stopped and no Days
Gone or CRS process remains. Next: inspect profile/save handling, complete
visible initial setup, and repeat with the mod restored.

**Previous Gaming Mode diagnostic (2026-10-25 15:19–15:23 CET):** after explicit
authorization to close Assetto Corsa and continue, the Deck was on the Steam
page for `Days-Gone-Coop-Test-Run-1`. Pressing Enter on its Play button
started the isolated Days Gone executable through Proton 11.0 with
`STEAM_COMPAT_DATA_PATH` set to the isolated `prefix-gamescope`; Steam tracked
the app's Proton and game PIDs. The CRS handler/uploader remained absent;
neither process started. No crash was induced, so crash-report suppression
is not validated.

Gamescope screenshots at launch, about 65 seconds later, and again after
roughly 2:40 continued to show Steam's loading/“Abort Game” screen. Meanwhile
the game process was running at high CPU use and had an active `:1` X11 window
named “Days Gone” (`steam_app_1259420`, 1920x1080); Steam's process log also
showed the app as tracked. The discrepancy is now reproducible with a Steam-
launched process, not only a direct launch.

All three debug commands (`status`, `ui-probe`, `net-probe`) acknowledged.
`ui-probe` found one mission manager (`Debug_UXTestingWidget=null`), no
UX-testing screen, a collapsed/unowned title-widget template with
`ShowButton=true` but `Button=null`, and a visible main-menu widget with no
rows (`MaxMenuOptions=0`, options=0, `mainPageActive=false`) and no focus.
`net-probe` found ORWorld and zero NetDrivers; no gameplay pawn was present.
No gameplay input or title transition was attempted.

I stopped only the exact isolated game process (PID 152503); its Proton
wrappers exited, and no Days Gone or CRS process remains. Temporary remote
diagnostic scripts and screenshots were removed. The user authorized closing
Assetto Corsa and continuing the isolated test; it is no longer foregrounded.
The previous 08:49 retry's minidump upload remains the only confirmed report
transmission; it was not downloaded. See the latest entry in
`REPLICATION-RESEARCH.md`.

### Isolated Run 1 staging

Isolated copies are at `~/TheysGone-coop-test/run1/game` on both PCs; the
live `BendGame/Saved` trees were excluded. Each has a separate Proton prefix.
The Deck's `Days-Gone-Coop-Test-Run-1` Steam shortcut was added through
Steam's UI and its launcher is restored to Proton 11.0. The host's latest
isolated launch used Proton Experimental; its named systemd unit is no longer
running (the stop request ended with `Result=timeout`, `ExecMainStatus=15`).
The Deck's latest test unit is inactive, and no game or crash-handler process
remained when checked. The Steam shortcut launches the isolated executable,
but Gamescope still shows Steam's loading screen while the game's X11 window
is active. Both copies, prefixes and their test data remain staged.

Earlier in Run 1, the host transferred save snapshot `81a5efc1` (225,546
bytes, 3 files), and the guest installed it under `TheysGone-guest-saves`.
That earlier connected phase sent production enemy data (`streamed=4`,
including Leon/crows), but the Deck had no local pawn or applied replica.
That is not evidence of production enemy-visual replication. The latest
startup retry was a separate Steam-launched run: neither client entered
gameplay, and no enemy-stream assertions were collected. A read-only check
of the isolated Deck copy and prefix found no obvious crash-upload toggle; none has been
found in the game INIs, but the CRS database setting was subsequently
identified and cleared with a reversible backup; the game resets it to
enabled at launch.

Earlier captures showed Steam launch UI, an Assetto Corsa splash, and Company
of Heroes 2. Assetto Corsa was closed with the user's authorization. The
latest isolated shortcut launch did start and Steam-track Days Gone, but the
Gamescope capture remained on Steam's loading screen despite an active game
X11 window. The Proton 10 comparison was inconclusive; the launcher remains
on Proton 11.0 and the isolated Days Gone process is stopped. The Steam
shortcut remains for further isolated testing.

Continue offline by tracing why the main-menu widget is visible but has no
entries, and why Steam/Gamescope does not display the `:1` Days Gone window
even when Steam tracks the launched process and X11 reports the game window
active. The co-op title-menu hook only appends its row when the game's menu
already has options; it returns early for an empty array. The guarded
`continue-title` path also refuses an empty menu, so neither action explains
the current state. The latest mission manager's `Debug_UXTestingWidget`
field was null; the title widget is only a Blueprint `WidgetTree` template
and its `Button` field is null. `ui-probe` reports these references plus UMG
viewport/visibility/focus state and main-menu cursor/first row without
calling transition functions. Its object count may include Blueprint
templates: use the full path and viewport/owner getters to distinguish them
from runtime screens. Do not invoke `continue-title` unless a real visible
title splash or a main menu whose first row is Continue is confirmed. The
isolated CRS handler/uploader binaries are disabled; their absence was
verified during non-crash runs, but do not deliberately crash to test report
suppression. Both isolated game processes are stopped. Once both clients
reach gameplay, use an in-game bot and collect paired `DMEN`/`DPOS` send,
receive and apply assertions. No production enemy-visual code change is
justified by this startup result. If the handler attempts an upload despite
the setting, stop and ask before continuing.

Co-op Anything's QA guidance applies: a real co-op test needs an in-game bot
and per-step state assertions on both clients. `game_remote.py` keyboard input
and compositor screenshots are setup diagnostics only, not QA evidence; a
desktop capture can show another application. If a scenario step is not
automatable, record it as `BLOCKED`/`SKIP`, never `PASS`. The existing debug
mailbox can provide in-game setup/probes, but any new bot action must run in
the game code rather than fake keyboard/mouse input.

| Item | State |
|---|---|
| Host listen server inside the live world | works (`net-listen`) |
| Guest join without a map reload | works (`net-connect`, LoadMap hook) |
| Connection stays open | works; Run I stayed open through more than six minutes of join, possession, AI spawn and cleanup |
| Guest keeps its own controller and Deacon | works (HandleClientPlayer wrapper) |
| Engine replicates to the guest | host GameState, 2 PlayerStates, host Deacon, host bike (simulated proxies) |
| Host in game while listening | works (`net-listen` in gameplay) |
| Native engine enemy pawn proxy | J: movement (~0.5 cm). L: stock registration omits Health. M: gated Health updates match. O: final Health=-320 and tear-off match; guest bIsDying is false, mesh/animation are null, native torn-off proxy survives host cleanup. This path is experimental and separate from production visuals |
| Production co-op enemy visuals | ASI code has `DMEN` asset paths, `DPOS` bone poses, guest hit gating and a 1.5 s silence timeout. Run 1 host sent nonzero `DMEN`/`DPOS` traffic during its connected phase; the guest had no local pawn. Guest receive/apply, appearance, death pose and expiry remain unverified |
| Guest pawn on the host (`net-possess`) | Run I matching Bend pawn succeeded; host and Deck probes showed `Playing`, a pawn and `MyBendPawn`; Deck pawn was `AutonomousProxy` |
| Run H crash isolation | Run I's unpossessed controller survived 2m27 without AI; with a possessed Bend pawn and verified host god mode, the AI test ran without a crash. Cause remains unproven |
| Guest drives host-owned pawn (`net-takeover`) | **Works for mouse and keyboard (Run G21).** With `PawnReplication=1` (both), `ClientAuthoritativePosition=1` (host), `ClientNavigation=1` and `ClientPossess=1` (guest), the guest pawn is AutonomousProxy, walks and turns, and the host's copy matches. Gamepad does not reach the guest pawn |
| Host survives guest leaving | yes (Run G9), guest PC and pawn are cleaned up |
| Guest NavigationSystem after join | `MovePendingLevel` clears it in this build. With `ClientNavigation=1` (Run E) it is recreated and stable, with `MainNavData=null`. That makes the door `UpdateCustomLink` path a no-op instead of a crash |
| Guest PlayerState | `PlayerState_2` (PlayerId 257) is in the active replicated GameState's `PlayerArray`, owned by the joined controller, and gets `ReceiveBeginPlay`. The controller's own `PlayerState` stays null and `OnRep_PlayerState` never fires. The host sends it (no active override); the likely cause is a never-remapped reference |
| Which streaming RPC really closed the connection | unknown; both directions are dropped |

No changes are committed. Preserve the existing unrelated README,
DEVELOPMENT.md, Poetry, and helper-file changes in the working tree.

## Test environment

| Machine | Access | Role | Notes |
|---|---|---|---|
| `sebastiaan@cachy` | `tools/re/rssh ssh sebastiaan@cachy bash -c "'...'"` | host | CachyOS, Proton, fish shell. Steam account evaclaessen, SteamID64 `76561198264177393` |
| `deck@steamdeck` | `tools/re/rssh ssh deck@steamdeck bash -c "'...'"` | guest | Steam Deck (previously reached at `192.168.1.178`). Steam account DrSkunk, SteamID64 `76561197999558802`. Goes offline when it sleeps |

- Password: the repo-root `sshpw` file (git-excluded). The Mac has no
  sshpass and no key auth, so `tools/re/rssh` (expect) is the way in. Plain
  `ssh` from Claude Code's `!` prompt fails: too many agent keys, and no TTY
  for the password.
- Claude Code's auto-mode classifier blocked shell writes on the Deck until a
  permission rule for the wrapper was added to `.claude/settings.local.json`.
  That rule pointed at the old scratchpad path; add one for
  `tools/re/rssh` in the new tool.
- Game folder on both machines:
  `~/.local/share/Steam/steamapps/common/Days Gone/BendGame/Binaries/Win64`.
- Crash dumps: `~/.local/share/Steam/steamapps/compatdata/1259420/pfx/drive_c/users/steamuser/AppData/Local/BendGame/Saved/Logs/unreal-v0-<session start>.dmp`.
  The file name is the **session start time**, not the crash time; compare
  modification times.
- INIs: both have `DebugTools=1`. The host's `[Research]` section has
  `ControllerClass=PlayerController`, `PawnClass=BendDefaultPlayerPawn_C`,
  `SpectatorJoin=1`. The Deck's has `ConnectUrl=steam.76561198264177393`. The
  Deck's original INI is saved as `TheysGone.ini.research-backup`; each machine
  has its previous plugin as `TheysGone.asi.prev`.

### Remote input and screenshots (installed 2026-10-22)

Both PCs now run `tools/game_remote.py` from
`~/.local/share/theys-gone-remote/game_remote.py`. The HTTPS endpoints are
`https://cachy:8765` and `https://steamdeck:8765`. Use the controller's
`control` subcommand for screenshots and bounded input sequences instead of
SSH for each action. See DEVELOPMENT.md's remote-controller section.

- Each installation has a private `token`, `remote.key` and `remote.crt`.
  Client tokens/public certificates are in the current Copilot session's
  private `files/game-remote-access/` directory (`cachy.token/.crt` and
  `steamdeck.token/.crt`), outside the repository. Never print their contents
  or put them in source control. Certificates expire after 30 days.
- User services: `theys-gone-input` (ydotoold, socket
  `/run/user/1000/theys-gone-ydotool.sock`, mode 0600) and `theys-gone-remote`.
  They are transient `systemd-run --user` units: running during this test
  session, not enabled for reboot. Stop both with
  `systemctl --user stop theys-gone-remote theys-gone-input`.
- Cachy capture uses `--capture spectacle` in the user's KDE session; the
  user manager supplies DISPLAY, WAYLAND_DISPLAY, XAUTHORITY and DBus.
  Cachy's UFW rule permits TCP 8765 **only from the controller Mac's
  192.168.1.65 address**, with comment `TheysGone remote controller`.
- Deck capture uses `--capture gamescope`, `WAYLAND_DISPLAY=gamescope-0`.
  `gamescopectl screenshot` captures the actual compositor output.
  FFmpeg X11 capture returned black despite the game running; do not use it
  as proof of a rendering failure.
- Deck input binaries were copied from Cachy's installed ydotool 1.0.4-2.1
  into the helper's `bin/` directory and verified runnable (their maximum
  required glibc symbol version is 2.34; the Deck has glibc 2.43).
  The Deck system partition/package database was not modified.
- A temporary ACL grants user `deck` read/write access to `/dev/uinput`.
  Original permissions were saved as `uinput-acl.before` in the helper
  directory. To restore after stopping the services:
  `sudo setfacl --restore="$HOME/.local/share/theys-gone-remote/uinput-acl.before"`.
  A reboot/device recreation also loses this temporary grant.
- Both services initialize `evdev` virtual gamepads. Action completion
  alone is not proof of game response: inspect before/after screenshots.
  Sequences remain sequential, with no independent input watchdog.
- Cachy: screenshots, keyboard, mouse movement/click and in-game controller
  Back were visually verified. Deck: screenshots, keyboard menu navigation
  and virtual-controller input to Steam's Play button were verified; the
  virtual controller did not close in-game Options. Use keyboard input on
  Deck until its in-game controller routing is resolved.

This preparation has not run `net-listen`, `net-connect`, `net-possess` or
`net-takeover`; it adds no new thin-client feasibility evidence.

### Deploy and restart (per machine)

```sh
tools/re/rssh scp -q package/TheysGone.asi <target>:/tmp/TheysGone.asi.new
tools/re/rssh ssh <target> bash -c "'d=\"\$HOME/.local/share/Steam/steamapps/common/Days Gone/BendGame/Binaries/Win64\"; pkill -f \"[W]in64/DaysGone.exe\"; for i in \$(seq 1 40); do pgrep -f \"[W]in64/DaysGone.exe\" >/dev/null || break; sleep 1; done; sleep 6; mv /tmp/TheysGone.asi.new \"\$d/TheysGone.asi\"; echo steam://rungameid/1259420 > ~/.steam/steam.pipe'"
```

- Launching goes through the Steam pipe. It sometimes needs a second write
  after about 90 s if no `DaysGone.exe` process appeared.
- Wait about 60 s after launch before the first debug command. The mod
  ignores requests already present at startup.
- Write `mv` over the ASI, never copy into it: the running game maps the file.

### Debug commands

Run on the target machine; the helper must be in `/tmp`
(`tools/re/rssh scp -q tools/debug_control.py <target>:/tmp/`):

```sh
cd /tmp; python3 -I /tmp/debug_control.py --game-dir "$d" <command>
```

- Results land in `TheysGone.log` (grep `research:`) and, for `net-probe`, in
  `TheysGone-net.txt`.
- `No acknowledgement` means the game thread is hung or the game crashed.
  Check for a new dump, then restart.

## Key findings to keep

1. Engine logging is compiled out. The only logger left is the fatal one
   (`DaysGone.exe+0x19c91b0`). Use `trace-on`/`trace-off` (ProcessEvent trace)
   to see what the engine does; it caught the streaming-RPC burst.
2. The default socket subsystem is SteamSockets. Only `USteamNetDriver` works;
   `IpNetDriver` fails to bind.
3. `UEngine::LoadMap` cannot run after startup on either side (crashes in
   `SetGameMode` / WorldSettings).
4. The mod's own Steam transport (`SteamPeer::ReceiveAll`) used to drain
   every P2P packet. It now skips that while `research::EngineOwnsP2P()`.
5. `UNetConnection::HandleClientPlayer` destroys the guest's Bend controller;
   Bend code then crashes looking for player 0's pawn. Wrapped.
6. Player two spawned through `RestartPlayer` crashes the host in Bend code.
   `SpectatorJoin=1` avoids it; the guest pawn must be spawned by the mod.

## Code map

| Where | What |
|---|---|
| `src/coop/research.cpp` `ProbeNetwork` | `net-probe`: drivers, connections (`CONN`), controllers/pawns (`PC`), world NavigationSystem (`WORLD`/`NAVSYSTEM`), roles/owners and `PROXY` actors, `CPF_Net` counts |
| `PatchNetDriverDefinitions` | points `Engine.NetDriverDefinitions` at `SteamNetDriver` |
| `NetListen` | host: patches definitions, optional class swaps and spectator join, calls `UWorld::Listen` (RVA `0x2c52660`); hand-built driver fallback |
| `HookedLoadMap` / `InstallLoadMapHook` | guest: inline hook on `UEngine::LoadMap` (RVA `0x2c013a0`), runs `MovePendingLevel` (engine vtable `0x4c0`) instead of reloading |
| `HookedHandleClientPlayer` / `InstallHandleClientPlayerHook` | guest: SteamNetConnection vtable slot 80 (RVA `0x29b1f40`), keeps the local Bend controller |
| `DropProcessEvent` / `SetStreamingRpcDrops` | drops `ClientUpdateLevelStreamingStatus`, `ClientFlushLevelStreaming`, `ServerUpdateLevelVisibility`; INI `DropStreamingStatus`, `DropLevelVisibility` |
| `NetPossess` | host: spawn a pawn next to the host and `Possess` it with the joined controller |
| `NetTakeover` | guest: `SetPlayer` (PC vtable `0xc70`) on the replicated controller, `ClientRestart`, view target; toggles back |
| `NetConnect` | guest: patches definitions, installs both hooks, `open <ConnectUrl>` |
| `src/coop/steam.cpp` `Poll` / kP2PSessionRequest | skips the P2P drain and accepts engine P2P sessions while the experiment runs |
| `src/coop/coop.cpp` `UpdateDebugTools` | command dispatch and `[Research]` INI reads |
| `src/coop/coop.cpp` `FindLocalController` | now ignores controllers whose `Player` is not a `LocalPlayer` |

## Analysis tools (`tools/re/`)

Python scripts take the game executable as their first argument. Run them on
cachy, where `DaysGone.exe` and `objdump` live (copy them to `/tmp` first).

| Script | Use |
|---|---|
| `rssh` | expect wrapper: `tools/re/rssh ssh|scp ...`, password from `sshpw` |
| `mdmp.py <dump>` | exception address, registers, executable/plugin frames per thread (runs locally) |
| `dis.py <exe> <rva>...` | function bounds from `.pdata`, referenced strings, disassembly around the RVA |
| `xref.py <exe> <string>...` | code that references a string (`L:` prefix for UTF-16) |
| `callers.py <exe> <rva>` | direct call/jmp sites of a function |
| `chain.py <exe> <reflection dump> <rva>...` | walks callers up to a UFunction native (needs a dump with `native=`) or a vtable entry |
| `vt.py <exe> <vtable rva>` | RTTI class lookup for a vtable slot (found nothing on this build) |
| `logfind.py <exe>` | finds the logging function from source-path call sites |
| `pakcfg.py <pak> <regex>...` | reads the pak index; entries are Oodle-compressed, so it cannot extract them |

- Plugin frames (`asi+0x...`) in dumps are resolved by building an unstripped
  copy: the `build.sh` command line without `-s`, then
  `x86_64-w64-mingw32-nm -C`. Optimised frames are often wrong, so treat them
  as hints.
- The newest reflection dump with `native=` addresses and `pflags=` is on
  cachy: `TheysGone-reflection.txt` in the game folder. Copies of it, the
  matching exe, and Run C logs/net probes/guest dump are also retained in
  the current session's private `files/runb-analysis/` directory.

## Next steps

Run C is complete. Offline analysis on 2026-10-23 identified the host fault
paths: Run A's Bend pawn casts its plain controller to `BendPlayerController`
and then dereferences the null cast result's camera-manager field; Run B's
`BendAmbientGenerator` dereferences the controller's null `MyBendPawn`
because the fallback pawn is only a generic `Character`. Run C tested the
matching native pair successfully through host possession and pawn arrival.

`ProbeNetwork` has since been updated locally to log each world's
NavigationSystem and `bAllowClientSideNavigation`, plus controller/pawn
roles, owners, `MyBendPawn`, PlayerState, and `RideVehicleComponent`.
Follow-up diagnostics bracketed `MovePendingLevel`, captured PlayerState
identities/array membership, and traced ProcessEvent calls. Offline
disassembly now shows the engine path explicitly clears the guest's
NavigationSystem; the PlayerState association remains unresolved. See the
research log for exact evidence and hashes.

### Current test status (2026-10-23)

Run A's default `BendDefaultPlayerPawn_C` possession crashed Cachy at
`DaysGone.exe+0x779962`. The `Character` fallback replicated to the Deck and
`net-takeover` acknowledged `ClientRestart` and view-target assignment, but
the Deck crashed within seconds at `DaysGone.exe+0x264ee02`. Neither outcome
establishes thin-client viability. The Deck crash reporter was dismissed
with **Don't Report**, and no report was submitted.

**Run B was attempted:** with `ControllerClass` empty and
`GuestPawnClass=Character`, listen/connect and in-game AI replication worked.
The host's `BendDefaultPlayerController_C` possessed `Character_0`, but Cachy
crashed at `DaysGone.exe+0x724a46` (`0xc0000005`, read `0x1f00`) within
seconds. The Deck's probe still showed `Pawn=null` and no Character proxy, so
`net-takeover` was not attempted. The host crash reporter was dismissed
with **Don't Report**, and no report was submitted. Cachy is stopped; its
INI was restored to the pre-Run-A version. The Deck game process remained
running after the host crashed and should be restarted before another test.

The pre-run save backups remain under
`~/TheysGone-research-backups/20261022-runA-pre/`. A copy of Cachy's
pre-Run-B INI is at
`~/TheysGone-research-backups/20261022-runB-pre/TheysGone.ini`. The
`0.3.0-dev` ASI remains installed on both PCs with SHA-256
`6512772bdadfd94ff7f54dc6dbf1e171c82746717b73938ca6449d2f1647fb51`.

**Run C:** empty `ControllerClass`, native `GuestPawnClass`, and
`SpectatorJoin=1`. Both PCs temporarily used `Transport=menu` to isolate
the engine experiment: their original UDP setup auto-connected in opposite
roles and transferred a save during the first preflight launch. After a
clean restart with UDP auto-connect disabled, native host possession
succeeded at 19:19:47; both games were responsive beyond 90 seconds.
Guest takeover acknowledged at 19:21:45; the Deck dump
`unreal-v0-2026.10.23-19.16.08.dmp` was modified at 19:22:02 and records
the null-navigation-system crash. No camera or movement verification passed.

Both games are now stopped. The Deck reporter was dismissed without
reporting, and both INIs were restored byte-for-byte to their originals.
Fresh backups are on Cachy at
`~/TheysGone-research-backups/20261023-190853-runC-pre/` and on Deck at
`~/TheysGone-research-backups/20261023-190854-runC-pre/`. No save restore
was performed. For future native-only tests, temporarily set `Transport=menu`
on both PCs before launch so the shipped hybrid cannot interfere.

**Run D diagnostic retest (2026-10-23):** Both PCs were backed up under
`~/TheysGone-research-backups/20261023T180214Z-diag-pre/`. The updated probe
build was deployed, then the original ASIs and INIs were restored after the
test. Both worlds had a BendNavigationSystem before connect. After
`net-connect`, the Deck's main world had `NavigationSystem=null`, while
Cachy's remained live (`bAllowClientSideNavigation=false` on both before
connect). The host spawned the native guest pawn successfully; the Deck
received it and two PlayerState proxies, but its controller and pawn still
had null PlayerState references.
`net-takeover` acknowledged, then the Deck crashed again at
`DaysGone.exe+0x264ee02`; camera/movement were not verified. Cachy later
crashed separately at `DaysGone.exe+0x1abfd5d`, not yet analyzed. Both
reporters were dismissed without submitting reports. Both games are stopped,
and original INI/ASI hashes are verified. Next: trace the client
LoadMap/MovePendingLevel navigation-system loss and why replicated PlayerState
actors are not linked to the guest controller/pawn. Do not treat command
acknowledgments as control evidence.

Do not repeat the incompatible Run A/Run B controller/pawn combinations.
The current results show that populated-world actor replication works, but
the experimental player-possession path is unsafe; they do not prove that a
full native-replication architecture is impossible.

**Run D follow-up (2026-10-23):** A diagnostic-only join isolated the Deck's
NavigationSystem loss to `MovePendingLevel`: its tail-call helper receives
the existing `UWorld` and null, calls an internal routine on the old system,
then writes null to the reflected NavigationSystem field at `UWorld+0x480`.
The same cleanup callback is used from the `UEngine::LoadMap` path; another
direct call to the replacement helper passes a non-null object, but its
identity/trigger is not resolved. This supports a same-world/transition-
cleanup mismatch as the cause, not a proven safe repair.
The host controller's `PlayerState_1` is `DrSkunk`/PlayerId 257; the Deck
receives the matching `PlayerState_2` proxy but does not place it in
`GameState.PlayerArray` or link it to the joined controller. The 60.2-second
ProcessEvent trace recorded no `OnRep_PlayerState`; this is a trace result,
not proof that all native replication paths were observed. The association
was unchanged in a later probe at driver time 49.15 s. No possession,
takeover or crash occurred; no explicit save, restore or edit was performed.
Both games were closed and the original ASIs/INIs were restored and verified
against the hashes in the research log.

**Offline follow-up (2026-10-23):**

- The non-null installer is stock `CreateNavigationSystem` (`0x26313f0`).
  `LoadMap` itself clears navigation in `MovePendingLevel` and recreates it
  only through `InitializeForWorld` when client-side navigation is allowed,
  so a native client would crash in the same door path.
- The research build adds `[Research] ClientNavigation=1` (default 0). It
  sets `bAllowClientSideNavigation` on the navigation class's default object
  and calls `InitializeForWorld(world, GameMode)` after `MovePendingLevel`.
- The probe now resolves weak `Owner` pointers, reports `World.GameState`
  and every GameState's `PlayerArray`.
- The trace counts `OnRep_*`, `ReceiveBeginPlay` and `ClientRestart` per
  object.

**Run E (2026-10-23, join-only, approved):** ASI `acf8e12f…` with
`ClientNavigation=1` on the Deck.

- The Deck recreated `BendNavigationSystem_1` after `MovePendingLevel` and
  stayed stable. `MainNavData` stays null.
- `PlayerState_2` is in the active GameState's `PlayerArray`, owned by
  controller `_1`, but `Controller_1.PlayerState` stays null.
- Both PCs were restored from `…-clientnav-pre` backups, hashes verified,
  no dumps.

The local build `3b7f0629…` adds a PlayerState/Pawn transition watcher to
the trace.

**Run F (2026-10-23, takeover, approved):** ASI `3b7f0629…`, host with
Bend's own controller and pawn classes, Deck with `ClientNavigation=1`.

- No crash on either PC for 30+ minutes through `net-possess`,
  `net-takeover`, a revert and a re-takeover.
- View target and HUD are on the guest Deacon, but input moves nothing.
  The trace shows `ClientRestart`, `ClientSetHUD`, `ClientSetViewTarget`
  and the server acknowledgements on `_C_1`, but zero `ReceiveTick` and
  zero input-axis events. The Deck's own `_C_0` keeps ticking. Replicated
  pawns also never actor-tick, although their components do.
- The guest pawn on the Deck is Role=SimulatedProxy. The host has it as
  RemoteRole=AutonomousProxy, owned by `_C_1`.
- `Controller.PlayerState` and the pawn's `PlayerState` stay null.
- Steam Deck gotcha: the SteamOS screensaver and the Steam overlay steal
  focus. Check `GAMESCOPE_FOCUSED_APP` is 1259420; gamepad B restores it.
- Both PCs were restored from the `-clientnav-pre` backups, hashes
  verified, no dumps, `/tmp` cleaned.

Next (superseded by Run G): the "no tick" reading was a trace artefact
(counts are per class, not per object), and the simulated role came from
Bend's empty `GetLifetimeReplicatedProps`.

**Run G (2026-10-23/24, G1–G21, user away with standing approval):** about
20 builds (latest `48664ec6`), host cachy, guest Deck.

- G1: forcing Role=2 on the Deck starts the full prediction loop
  (`ServerMove`/`ClientAckGoodMove`).
- G6: the Deck crashed in `CancelCurrentPlayerNavigation` because `PC+0x2078`
  is created only in Bend `Possess` → `ClientPossess=1`.
- G7: pawns replicated nothing because of an empty `GetLifetimeReplicatedProps`
  → `PawnReplication=1`. The guest pawn then arrives AutonomousProxy with
  PlayerState and walks on the client.
- G8–G9: the server rejected root-motion moves → `ClientAuthoritativePosition=1`
  (plus `MAXPOSITIONERRORSQUARED=1e12`). Movement is now in sync.
- G10–G16: look input went to the Deck's old pawn through the shared static
  input stack → `net-takeover` skips the own PC's `ProcessPlayerInput` and
  adopts its Blueprint input components.
- G17–G21: the camera only takes mouse input for player 0 → `net-takeover`
  promotes the guest PC to index 0 of `World+0x560`. Turning matches the
  host (20°/800 px).
- Gamepad sticks do nothing on the guest (likely Steam Input).
- The host survives the guest leaving.
- Both PCs were restored and the hashes verified; `/tmp` was cleaned.

**Run H (2026-10-24, AI spawn probe):** the host spawned `SwarmerPawn_C_30`.
Its health changed from 200 to 160 and it moved 505.6 cm in the host world.
The host had 31 replicated swarmer pawns; the guest probe reported 30
aggregate swarmer proxies but did not identify that actor or its property
updates. About 19 seconds later, Cachy crashed at `DaysGone.exe+0x359dc9`
inside `ABendAmbientGenerator::OnPlayerRespawn` while binding its
`OnPlayerTeleported` delegate. The helper follows the generator's cached
controller at `+0xcf0` to `BendPlayerController.MyBendPawn` at `+0x8d8`;
that pawn pointer was null, producing the invalid delegate address `0x20d0`.
The host probe showed that the local controller still had its pawn but the
joined controller was Spectating with `Pawn=null` and `MyBendPawn=null`.
After the swarmer spawned, host health fell from 121.875 to 56.25; the debug
session then ended, `local pawn: none` was logged, and the crash followed.
This timing is consistent with enemy damage causing a host death/respawn,
but the unpossessed guest controller is another plausible null source.
Guest join, enemy damage, and player respawn were not isolated. The crash
report was dismissed without submission. Both games were closed, the
original ASI/INI hashes were verified, and temporary files were removed.
The dump and logs are in the session's private `files/rg/`.

The same dump's runtime metadata identifies `UE4 BendGame 4.11.1-0+UE4`.
Earlier references to UE 4.10.1 are source-pattern comparisons, not the
exact version string reported by this build.

**Run I (2026-10-24):** the join-only phase survived 2m27 with the joined
controller unpossessed. `net-possess` then gave it a matching Bend pawn on
the host and Deck. A verified host `god-on` survived a native damage probe
without health loss. One host-spawned swarmer changed host/Deck counts
30→31 and 15→16, respectively; cleanup restored both counts and the
connection stayed open. The host swarmer moved 724.7 cm and its health
changed 200→160 while host health stayed 121.875. No crash or new dump was
observed. Exact guest-side actor identity/property updates remain unknown.
Save archives had to be restored on both PCs, after which all save and
original ASI/INI hashes verified; games and temporary backups were cleaned
up.

**Run J (2026-10-24):** the per-actor probe confirmed that a host-spawned
swarmer appears as a guest simulated proxy with `bReplicateMovement=true`.
At the paired sample, host `_30` was at `(-29917.4, -206043.8, -9916.2)`
with health 156.25, while guest `_60` was at `(-29917.0, -206044.0,
-9916.0)` with health 200. The positions differ by about 0.5 cm. A native
host damage probe reduced health from 160 to 156.25 without updating guest
health. The guest's local `_30` was a different actor at `(100, 100, 100)`,
so actor suffixes alone are not reliable cross-peer identities. The host
test actor was destroyed on `end`; the guest proxy was absent from the
post-cleanup probe. The Deck's save-block guard still rejected `begin`, and
no Deck UI input was sent. Both games were closed, all save hashes matched
their pre-run manifests after restoration, and the original ASI/INI files
matched their backups.

**Runs K-M (2026-10-24, user away with standing permission):**

- K: `bReplicateHealthToAll` was already true on both peers. Host health
  reached 153.25 while the guest proxy stayed at 200.
- L: diagnostic wrapper observed 17 stock lifetime entries; Health at
  RepIndex 22 was absent on both peers, including swarmer and player classes.
- M: gated `BendHealthReplication=1` added Health with `COND_None`. Two
  post-spawn hits matched on both PCs (156.25, then 152.5); the guest trace
  recorded three `OnRep_Health` calls on the received proxy.
- `OnRep_Health`'s native virtual target and `OnRep_LastTakeHitInfo`'s native
  implementation are empty. Health transport is fixed experimentally, not
  death or damage presentation.
- Both PCs' save/config/plugin files were restored and hash-verified after
  each completed run. Current backups and evidence are under
  `~/TheysGone-research-backups/20261024T1525Z-runK-pre`; local evidence is in
  the session's private `files/runK/`.

**Runs N/O (2026-10-24):**

- N: host test swarmer had a mesh/animation instance, while the guest proxy
  had neither. Lethal damage during a grapple clamped Health to 1; the
  command correctly returned an error.
- O: damage 1.5 seconds after a fresh owned spawn caused native death.
  Both peers had Health=-320, IsAlive=false and bTearOff=true. Host
  bIsDying=true; the native guest pawn proxy had bIsDying=false,
  SkeletalMesh=null and AnimInstance=null.
- The guest's native tear-off changed Role/RemoteRole to 3/0 and stopped
  replication. Its detached native proxy remained after host cleanup.
  This is not evidence about the separate custom poseable replica.
- Both runs used `Transport=menu`. Run O logs show zero custom pose traffic
  and no custom `perf: enemies`/replica lifecycle logs. The production ASI
  already has an appearance+pose stream (`DMEN`/`DPOS`), guest hit gating,
  and silence-based replica cleanup; this path was not run.
- `GameplayStatics.ApplyDamage` is void in this build. Earlier
  `returned=0` diagnostics came from reading an absent reflected field;
  probes now report only measured Health.
- Both games are **stopped** and all original save/config/plugin files
  are restored and hash/manifest verified. No new crash dumps were found.
  Latest tested local build:
  `4eb28055b5cb52bed3bcbfd1ee6b29e0e8cac4c3591db46cebe0101b27225bd4`.
  `make build`, `make test` (67 Python tests plus C++), and diff checks pass.

Next: apply the Co-op Anything QA method to the existing `DMEN`/`DPOS`
path using a scripted bot and isolated game copies, not the owner's live
installs/saves. Require nonzero custom send/receive/apply counters, then
test one enemy, two different appearances, guest hit dedupe, host death
pose, replica expiration after source removal, packet loss/reorder, asset
late-load and reconnect. Add a code fix only for a reproduced failure.
Keep `PawnReplication=1` and `BendHealthReplication=1` identical before
native-driver experiments; restart both copied clients to change layouts.
Remaining major work includes gamepad/Steam Input, player-0
side effects (HUD, minimap, menus, missions), suppressing the guest's
independent world simulation, and a guest-to-host interaction channel.
