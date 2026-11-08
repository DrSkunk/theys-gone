# Co-op research log

Dated live-test and offline-analysis evidence for Theys Gone. Entries record
what was observed at the time and may be superseded by later entries. Current
status and priorities are in the [co-op roadmap](COOP-ROADMAP.md);
native-replication conclusions are in the
[research handoff](REPLICATION-HANDOFF.md).

Sections are not in chronological order. Index by date:

| Date | Section |
|---|---|
| 2026-10-21 | Native-replication research (not shipped); Probe results; Phase 1 log |
| 2026-10-22 | Remote-control preparation; Partial Run A, Character pawn fallback, Run B |
| 2026-10-23 | Run B crash-site analysis; Run C; Run D and follow-up; Offline client navigation follow-up; Network RPC surface census; Run E; Run F |
| 2026-10-23/24 | Run G: thin-client movement and look |
| 2026-10-24 | Runs H to O |
| 2026-10-24/25 | Steam Deck isolated Run 1 bootstrap; paired production load; late-adoption menu rebuild |
| 2026-10-25 | Live-save fixture; Enemy pack replication; Mission discovery and passenger boarding |
| 2026-10-26/27 | Live mission lab |
| 2026-10-27 | Repeated paired resync attempt; Independent roaming; Free roam; Gradual enemy ownership handoff; Story mission catalog |
| 2026-10-28 | Passenger seat in a paired session; Stealth missions; Partner avatar during cutscenes |
| 2026-10-28/29 | Paired mission copies, frame peaks and save reloads |
| 2026-10-29/30 | Partner on the minimap; Host's story companion; Time and weather |
| 2026-10-30 | Protocol-v9 proxy second attempt; Host mission on the guest; Host mission weather; Region transition; Companion materials; Companion vehicles |
| 2026-10-31 | Weapon-wheel and pause timing; Shared menu pause; Co-op review scenarios |
| 2026-11-01 | Native host load and fast travel; Death, sleep and travel follow-up |
| 2026-11-06 | Join block, no-INI v12 join and native PLAYER SETTINGS; Title-menu settings, F9 boarding and the leave notice; Two-PC Steam session without an INI |

## Weapon-wheel and pause timing investigation (2026-10-31)

Read-only analysis used the isolated `run1` executable, verified against
Steam build 19221447 (SHA-256
`17c2fca01d566914147a6d4924d038cb1f5a822c2829e4c45862e2bf837730b3`),
and a fresh full reflection dump from a disposable `time-control` lab clone.
No real game install or saves were modified.

The runtime controller Blueprint exposes `WeaponWheel_DID` (IntProperty),
`WeaponWheelTimeDilation` (FloatProperty), and the native bitfield
`WeaponWheelActive`. `OpenWeaponWheelAction` has locals for
`GetGameSpeedFloat`, multiplication, and `SetBendTimeDilation`'s output ID.
These are resolved by reflection in the implementation, not hard-coded
offsets.

Static disassembly confirms `BendGameplayStatics.SetBendTimeDilation`'s
native thunk at RVA `0x13beab0` calls `0x5bd0f0`. Requests are stored as
float/ID pairs; the lowest request determines world dilation.
`ResetBendTimeDilation`'s thunk at `0x13b7eb0` calls `0x5b9f70`, which removes
only the matching request before recomputing dilation. Removing the wheel's
own request therefore does not clear focus, combat, or other requests.
Setting global dilation to 1, or resetting all requests, would not provide
that isolation.

The plugin now removes the wheel request while connected, checks again after
controller events so an opening wheel is handled promptly, and recreates the
request if the partner disconnects while the wheel is open. No INI values,
game files, or network packet layouts are changed. The debug-only
`time-probe` reports the wheel and focus request IDs, wheel scale, global
dilation, and pause/camera-tick state.

| Path found | Evidence and current policy |
|---|---|
| Weapon/crafting wheel | Dedicated `WeaponWheel_DID`; only this request is removed while connected |
| Focus aiming | `FocusSlowMo_DID`, `FocusTimeDilation`; kept unchanged |
| Vehicle aiming | `VehicleManualAimTimeDilation`; separate native request path, kept unchanged |
| Melee hit effects | `GetTimeDilationValueForMeleeCriticalHit`, `GetTimeDilationValueForMeleeHitOnBike`; kept unchanged |
| Native pause menu | `OpenPauseMenu`, `TogglePauseMenu`, `GetPausedStatus`, `SetPauseWithCameraTick`; shared pause was added afterwards (next section) |
| Focus loss | Controller `PauseOnFocusLost`/`PauseOnFocusGained` and a static focus-loss handler; unchanged |
| Cinematic pause | `TurnOnCinematicPause`, world-settings `MatineeTimeDilation` and `Pauser`; not overridden |
| Map/inventory | Both observed with `paused=1`, `camera-tick=0`, `global=1.0000`; this is a true local pause, not a wheel-style dilation request |
| Other scripted menus | Need individual in-game observations |

### Isolated single-game smoke test

One real game in the disposable `time-control` clone, plus
`tools/fake_peer.py` on loopback UDP, exercised the connection transitions.
This is **not a paired game or two-PC validation**. The final candidate ASI
SHA-256 was
`1df5b0baf889f80748324e386837a85b21080930930115b686a1b91fc3cb117f`.

| Check | Recorded result |
|---|---|
| Standalone wheel (Q held) | Final log `21:16:33.791`: `wheel=1`, request `-2147483647`, `global=0.2000` |
| Join with wheel already open | `21:16:50.925`: peer connected; `21:16:50.941`: removed wheel request `-2147483647` |
| Preserve another slowdown | A separate `SetBendTimeDilation(0.1)` request `-2147483646` remained active after joining (`21:16:59.226`) and closing the wheel (`21:17:00.774`): `global=0.1000` |
| Reopen while connected | After explicitly removing only the injected request, `21:17:02.808`: visible wheel, `wheel=1`, `global=1.0000` |
| Disconnect with wheel open | `21:17:30.936`: restored request `-2147483644`; `21:17:44.040`: `wheel=1`, `global=0.2000` |
| Close after disconnect | `21:17:45.557`: `wheel=0`, `global=1.0000`; no stuck slowdown |
| Escape pause menu and M map | First candidate log `21:11:27.230` and `21:11:30.316`: `paused=1`, `camera-tick=0`, `global=1.0000`, with the simulator connected |
| I inventory | Final log `21:17:47.093`: `paused=1`, `camera-tick=0`, `global=1.0000`, playing alone |

The first candidate also demonstrated connected opening/closing at
`21:10:54.130` / `21:10:56.197` and disconnect restoration at
`21:12:20.143`. The final candidate adds guards for invalid scales, the
native scale-1/no-request case, and a missing restoration ID. The native
request IDs are negative in these runs; only `-1` is treated as invalid.

Logs and screenshots (`baseline.log`, `candidate-first.log`,
`candidate-final.log`, wheel, pause, map and inventory captures) are archived
outside the repo as `<archive>/time-control-20261031/`. The base ASI matched
the clone's pre-test backup
(`251025671c43a00cb850a80da76ec88ba7755767e42e440132a2dbb294539945`), as did
the base INI; the clone was removed afterwards.

The injected request proves selective removal, not real focus/combat overlap.
An aim/middle-click attempt did not activate focus (`focus-request=0`); it is
not counted as a focus test. Pause/map/inventory were unchanged in this
candidate; the shared menu pause is verified in the next section.

## Shared menu pause: paired lab verification (2026-10-31)

User-selected policy: one player's pause menu keeps gameplay running; both
players opening pause/map/inventory pauses both worlds. Closing either menu
resumes. Protocol v11 adds state flag `kMenuPause` without changing the
145-byte layout; v10 peers are rejected. Standalone/disconnected games keep
native pause and wheel timing.

Two **real game instances on one PC**, using disposable `pause-host` and
`pause-guest` reflink clones on the host PC, connected over loopback UDP
27883/27884. The guest used the existing sandboxed Steam client and native
host-save transfer (`HostSave=1`). Both gameplay pawns had health 121.875 at
`21:46:16.290` / `21:46:16.592`, with partner avatars/poses streaming.
This is paired in-game validation, **not a separate-PC or Steam-network test**.
Both ASIs had SHA-256
`49b4311def3b59db7e04ac0d6e869a4695252603b9317248f9989d970b3f9144`.

Reflection and live probes established two complementary queries:
`BendPlayerController.IsPauseMenuActive` detects Escape but not map;
`UIManager.IsPauseMenuActive` detects map/inventory but not Escape.
The implementation uses their logical OR, not the world's effective paused
flag, to publish menu intent. This keeps a vote active after suppressing the
native pause and avoids a feedback loop.

`GameplayStatics.SetGamePaused(false)` returned false even though
`GetPausedStatus` confirmed a successful unpause in the diagnostic run.
The final candidate verifies the actual pause state after invocation instead
of interpreting that return as failure. No hard-coded engine offsets,
global-dilation override, or full object scans were introduced.

| Paired check | Evidence |
|---|---|
| Host Escape only | `21:46:18.868`: host `paused=0 local=1 peer=0`; `21:46:20.151`: guest `paused=0 local=0 peer=1` |
| World keeps running with one menu | `GetTimeSincePlaythroughStart` advanced host 2473.36 → 2476.85 (`21:46:19.516` / `21:46:23.131`), guest 2417.86 → 2420.81 (`21:46:20.800` / `21:46:23.884`) |
| Both Escape menus | `21:46:37.228` host and `21:46:38.542` guest: `paused=1 local=1 peer=1`; both menus remained visible |
| Both worlds actually freeze | Host playthrough time stayed exactly 2488.65 from `21:46:37.859` to `21:46:43.456`; guest stayed exactly 2431.77 from `21:46:39.190` to `21:46:43.789` |
| Network stays responsive during shared pause | Host `21:46:43.496`: send 29.4/s, recv 28.6/s; guest `21:46:41.521`: send 28.5/s, recv 29.3/s |
| Host closes; guest Escape stays open | `21:46:46.362` host / `21:46:46.705` guest: both `paused=0`; guest still `local=1 peer=0` |
| Host map only | `21:47:12.247` / `21:47:12.592`: both `paused=0`, host vote 1, guest vote 0 |
| Host map + guest inventory | `21:47:14.798` / `21:47:15.646`: both `paused=1 local=1 peer=1`; screenshots verify the two different menus |
| Guest closes inventory; host map stays open | `21:47:18.363` / `21:47:18.726`: both `paused=0`; host still `local=1` |
| Host wheel only | `21:47:20.406` / `21:47:20.776`: host `wheel=1`, guest `wheel=0`, both `global=1.0000 paused=0` |
| Both wheels, then close | `21:47:21.936` / `21:47:22.812`: both `wheel=1 global=1.0000`, no menu-pause votes. `21:47:24.479` / `21:47:24.856`: both closed, global still 1 |
| Disconnect with host menu open | Before guest stop, `21:47:53.472`: host `paused=0`. Timeout `21:47:58.507` restored native pause; `21:48:02.627`: `paused=1 connected=0` |
| Standalone restoration | Close at `21:48:04.176`: `paused=0`. Wheel open `21:48:05.710`: `global=0.2000`; close `21:48:07.259`: global 1 |

The final logs contain no menu timing-control failures. Evidence
(`pause-host-final.log`, `pause-guest-final.log`, diagnostic-run logs and
screenshots) is archived outside the repo as
`<archive>/shared-pause-20261031/`. Base ASI/INI and the complete base Saved
tree matched each clone's pre-test backup; the clones were removed.

The F8 overlay and wheel do not request a shared pause, and no blanket unpause
is applied outside a controlled menu. Open items: see
[COOP-ROADMAP.md](COOP-ROADMAP.md).

## Native-replication research (not shipped)

The sections from here through Run O (2026-10-21 to 2026-10-24) test whether
Theys Gone could use Unreal's own listen-server replication (`UNetDriver`,
actor channels, property replication, RPCs) instead of, or underneath, its
custom datagram layer described in [HOW-IT-WORKS.md](HOW-IT-WORKS.md). None
of it is shipped; see [REPLICATION-HANDOFF.md](REPLICATION-HANDOFF.md) for the
conclusions.

## What the build contains

All evidence is from build 19221447 under Proton: the runtime reflection dump
(`dump-reflection`), the object census (`dump-objects`), and a string scan of
`DaysGone.exe`. No configuration inside the pak could be read: the pak uses
Oodle compression (method 4), so `DefaultEngine.ini` is only observable at
runtime (see `net-probe`).

### The engine's networking stack is compiled in

Reflected classes present at runtime:

| Area | Classes |
|---|---|
| Drivers | `Engine.NetDriver`, `OnlineSubsystemUtils.IpNetDriver`, `Engine.DemoNetDriver` |
| Connections | `Engine.NetConnection`, `OnlineSubsystemUtils.IpConnection` |
| Channels | `Engine.Channel`, `ActorChannel`, `ControlChannel`, `VoiceChannel` |
| Travel/session | `Engine.PendingNetGame`, `GameNetworkManager`, `GameSession`, `OnlineSession`, `OnlineSessionClient` |
| Package map | `CoreUObject.PackageMap`, `Engine.PackageMapClient` |
| Beacons | `OnlineBeacon`, `OnlineBeaconHost`, `OnlineBeaconClient`, `PartyBeacon*` |

Executable strings confirm the implementation, not just the type registry:
`UNetDriver::InitListen`, `UNetDriver::InitConnect`,
`UNetDriver::ProcessRemoteFunction`, `UNetConnection::InitRemoteConnection`,
`UNetConnection::Tick: Connection TIMED OUT`, `[UActorChannel] Actor: %s, Role: %i, RemoteRole: %i`,
`ReplicateProperties: Compare changelist value mismatch`, the control messages
`Welcome`, `Challenge`, `Netspeed`, `Upgrade`, `PCSwap`, `ActorChannelFailure`,
`Listening on port %d`, `Connecting to %s (%s) port %d`, `bind() failed`, the
travel failures `ETravelFailure::PendingNetGameCreateFailure/ServerTravelFailure`,
`SpawnDefaultPawnFor`, `K2_PostLogin`, `ChoosePlayerStart`, and the full
`net.*` console variable family (`net.RPC.Debug`, `net.Replication.DebugProperty`,
`net.ListActorChannels`, `net.MaxPlayersOverride`, ...).

Stock engine classes keep their replication surface: `Actor` has `Role`,
`RemoteRole`, `bReplicates`, `bReplicateMovement`, `ReplicatedMovement`,
`NetUpdateFrequency`, `SetReplicates`, `ForceNetUpdate`;
`CharacterMovementComponent` has the whole `ServerMove*` / `ClientAdjustPosition*`
RPC family; `PlayerController` has its `Server*` RPCs; `GameState` has
`ReplicatedWorldTimeSeconds`. `GameNetworkManager_0`, a `GameSession` and a
`PlayerState` are live in the persistent level even in single player.

### Steam and EOS net drivers are linked but not registered

The executable contains `/Script/OnlineSubsystemSteam`, `USteamNetDriver`,
`USteamNetConnection`, `SteamSockets`, `SteamNetworking006`, the
`k_EP2PSessionError*` strings, and `/Script/OnlineSubsystemEOS`. Neither module
appears in the reflection dump, so they are not started in this build: a
listen server would use `IpNetDriver` over UDP unless the module can be
loaded, which is a separate research item.

### Bend's gameplay is almost entirely single-player code

| Package | UFunctions | With `FUNC_Net` |
|---|---|---|
| `/Script/Engine` | 2747 | 88 |
| `/Script/BendGame` | 8182 | 11 |
| `/Script/GameplayAbilities` | 259 | 26 |

The 11 Bend RPCs are template leftovers from Epic's ShooterGame lineage:
`BendPawn.ServerEquipWeapon/ServerHolsterWeapon`,
`BendPlayerPawn.ServerSpawnEnemy/ServerSpawnDumbEnemy/ServerSpawnTestBox`,
`BendPlayerController.ServerCheat/ServerCheatAll/ClientPawnDied`,
`BendWeapon_Instant.ServerNotifyHit/ServerNotifyMiss`,
`BendMovementComp_VehicleNoDrive.BendServerUpdateState`. The matching
`OnRep_` functions exist on `BendPawn` (`OnRep_Health`, `OnRep_CurrentWeapon`,
`OnRep_LastTakeHitInfo`), `BendWeapon_Instant` (`OnRep_HitNotify`),
`BendRideVehicleComponent` (`OnRep_VehiclePawn`) and `BendPRI` (a `PlayerState`
with `Deaths`, `MonsterKills`).

Everything that makes Days Gone a game lives in `BendGameMode`:
`MissionManager`, `JobManager`, `WorldManager`, `AmbientEventManager`,
`SectorDataManager`, `PlayerRespawnManager`, `FogOfWar`, `UnlockManager`,
save mode flags, `Buddy`, `CurrentPlayerVehicle`. In Unreal, `GameMode` only
exists on the server, so this is already host-authoritative by construction.
The flip side: a joining client has no `GameMode`, and the game code that
reads these managers through the local game mode would find nothing.

The class chains are standard: `BendDefaultPlayerPawn_C > BendPlayerPawn >
BendPawn > Character > Pawn > Actor` and `SwarmerPawn_C > BendSwarmerPawn >
BendFreakerPawn > BendAIPawn > BendPawn > Character`. Every pawn is a
`Character`, so movement replication and `ServerMove` exist for all of them.

## Probe results (2026-10-21, main menu)

`net-probe` (DebugTools, writes `TheysGone-net.txt`) answered the unknowns:

| Question | Answer |
|---|---|
| `Engine.NetDriverDefinitions` | one entry: `GameNetDriver -> /Script/OnlineSubsystemGOG.NetDriverGOG`, fallback `/Script/OnlineSubsystemUtils.IpNetDriver` (the GOG class is absent on Steam, so the fallback applies) |
| `World.NetDriver` | null in single player; `GameNetworkManager_0` present |
| Roles | GameMode `Role=3 RemoteRole=0 bReplicates=false`; GameState, PlayerState, PlayerController `Role=3 RemoteRole=1 bReplicates=true` |
| Actors with `bReplicates` | 172 of 2155 at the menu: every AI pawn class (`SwarmerPawn_C`, `HordeSwarmerPawn_C`, wolves, deer, crows, survivors), `BendDefaultPlayerPawn_C`, the bike, `BendWorldSettings` per streamed level, `MatineeActor` |
| `CPF_Net` properties | Engine 80 in 22 classes; BendGame **17 in 8 classes** (`BendPawn` 5, `BendPlayerController` 3, `BendPRI` 4, plus one each on `BendMovementComp_VehicleNoDrive`, `BendRideVehicleComponent`, `BendWindGust`, `BendWeapon_Instant`, `VehicleController`) |
| Steam net driver | `SteamNetDriver` and `SteamNetConnection` classes are live at runtime (loaded after the earlier reflection dump) |

`dump-reflection` now prints `pflags=` and a `net` marker per property.

## Phase 1 log: getting a net driver up

1. **`open /Game/Maps/ORWorld/ORWorld?listen` crashes.** The engine's `LoadMap`
   path reaches `UWorld::SetGameMode`, which reads
   `GetWorldSettings()->DefaultGameMode` and finds no world settings for the
   freshly loaded map (`DaysGone.exe+0x2c5763c`, null + 0x378). The game never
   uses `LoadMap` after startup (menu-in-world, Continue loads in place), so the
   host must not reload. The crash site is only reached when `IsServer()` is
   true, so a joining client may still survive `LoadMap`; untested.
2. **`UWorld::Listen` is at RVA `0x2c52660`** (NetDriver check,
   `CreateNamedNetDriver` / `FindNamedNetDriver` / `SetWorld`, virtual
   `InitListen` at vtable slot `0x230/8`, `MaxInternetClientRate > 2500` clamp,
   `lanplay`). Calling it on the live world created `IpNetDriver_0` through the
   fallback definition, but `InitListen` failed and Listen dropped the driver.
3. **The failure text is `SteamSockets: binding to port 7777 failed (0)`.**
   `OnlineSubsystemSteam` registers its socket subsystem as the default, so an
   `IpNetDriver` cannot bind. (`InitListen` takes `(Notify, URL&, bool
   bReuseAddressAndPort, FString& Error)`; the error reference is the fifth
   argument and travels on the stack.)
4. **`USteamNetDriver` works.** `GameplayStatics.SpawnObject(SteamNetDriver,
   GameEngine_0)`, `UNetDriver::SetWorld` (RVA `0x29d0400`), `InitListen` with a
   default `FURL` (port 7777) returned true, and `World.NetDriver` was assigned.
   The game kept running at the menu with
   `/Engine/Transient.GameEngine_0.SteamNetDriver_0` attached. This driver is
   Steam P2P: clients connect with `open steam.<host SteamID64>` and get relay
   and NAT traversal from Steam. The driver is not registered in the engine's
   `FNamedNetDriver` list, so `FindNamedNetDriver` does not see it.

5. **The client cannot `LoadMap` either.** `open steam.<id>` on the Deck
   reached the host (`PendingNetGame`, `Welcome`), then `LoadMap` crashed in
   the same place. Fix: `net-connect` installs an inline hook on
   `UEngine::LoadMap` (RVA `0x2c013a0`). When a pending game is passed, the
   hook runs `UEngine::MovePendingLevel` (vtable slot `0x4c0/8`, which moves
   `Pending->NetDriver` into `World.NetDriver`, renames it `GameNetDriver` and
   calls `SetWorld`) on the world that is already running and returns success;
   `TickWorldTravel` then sends `NMT_Join` as usual. Both sides also need
   `Engine.NetDriverDefinitions` rewritten to `SteamNetDriver` (via
   `KismetStringLibrary.Conv_StringToName`), and the mod's Steam transport must
   accept the engine's P2P session request.
6. **The join handshake completes.** With the hook, the Deck ended up with
   `ServerConnection=SteamNetConnection_0` and a replicated
   `PlayerController` (`Role=2 RemoteRole=3`) owned by that connection: Hello,
   Challenge, Login, Welcome, Join and `SpawnPlayActor` all ran through Steam
   P2P between two PCs.
7. **The host crashes in its own player-spawn chain.** Three joins, three
   different crash sites, all inside `RestartPlayer` for player two while the
   host sat at the main menu: a Bend `BendDefaultPlayerController_C` virtual,
   a physics query during the new pawn's first tick, and
   `GameMode.InitStartSpot` (Bend override). Swapping
   `GameMode.PlayerControllerClass` to the engine `PlayerController` removed
   the first; `bStartPlayersAsSpectators=1` skips `RestartPlayer` entirely and
   removed the rest. The guest's pawn will have to be spawned by the mod.
8. **The mod's own Steam transport ate the engine's packets.**
   `SteamPeer::Poll` drained P2P channel 0 every frame and discarded anything
   not from the mod's partner. It now leaves the channel alone once
   `net-listen` / `net-connect` ran.
9. **Level-streaming RPCs killed the connection.** A ProcessEvent trace across
   the join showed the host sending 1798 `ClientUpdateLevelStreamingStatus`
   RPCs (one per streamed level) and the guest answering with
   `ServerUpdateLevelVisibility`, then `K2_OnLogout` within a second. Engine
   logging is compiled out (only the fatal-error logger remains), so the trace
   was the only way to see it. Dropping both RPCs and
   `ClientFlushLevelStreaming` in the ProcessEvent hook keeps the connection
   open. Each PC streams its own world, so nothing is lost.
10. **The client destroyed its own controller.**
    `UNetConnection::HandleClientPlayer` (RVA `0x29b1f40`, SteamNetConnection
    vtable slot 80) destroys the local `BendDefaultPlayerController_C` and
    makes the replicated controller local; Bend code then asks for player 0's
    pawn, gets null and crashes (`DaysGone.exe+0x57a906`). A vtable wrapper
    hides the local controller during the call and restores it afterwards: the
    replicated controller keeps `Role=2`, its `NetConnection` and the
    connection's `State`/`PlayerController`/`OwningActor`, but no viewport or
    input.

**Result (2026-10-21, host at the title menu, guest at the title menu):** a
stable engine connection over Steam P2P between two PCs, alive after 60 s on
both sides with 9 open channels and no crash. The guest keeps its own Deacon
and controller. Replicated to the guest by the engine itself, without any mod
code: the host's `BP_BendGameState_C`, two `PlayerState`s, **the host's
`BendDefaultPlayerPawn_C`** and **its bike `BP_bike04Drifter_C`** (all
`Role=1`, simulated proxies), plus the guest's own `PlayerController`
(`Role=2`).

Later runs answered the open questions from this point: the host's Deacon
and populated-world AI replicate to the guest as proxies (Runs B to J).

INI knobs for the experiment: `[Research] ConnectUrl=steam.<host id>` on the
guest; `ControllerClass=`, `PawnClass=`, `SpectatorJoin=` on the host. Later
added: `ClientNavigation=1` on the guest (see "Offline follow-up: client
navigation lifecycle and probe artifacts"). The
probe now also lists `PROXY` actors (Role other than Authority), which on the
guest shows what the host replicates.

The original phased plan, step table and recommendation written here are
superseded; current native-replication conclusions are in
[REPLICATION-HANDOFF.md](REPLICATION-HANDOFF.md).

## Remote-control preparation (2026-10-22)

An authenticated HTTPS helper for screenshots and virtual input was installed
on both PCs (test infrastructure; no `net-*` command was run and no ASI or INI
changed). On the host PC, KDE Wayland capture and keyboard, mouse and virtual
gamepad input worked in the menus. On the Steam Deck only `gamescopectl
screenshot` captured the real output (X11 capture was black), and virtual
gamepad east/B did not close Options, so in-game virtual-controller routing on
the Deck was unverified.

## Partial Run A (2026-10-22)

**Status: incomplete; this is not a thin-client result.** Both games were
initially at their title menus. Before entering gameplay, each PC's Proton
`Saved` tree, game `Saved/SaveGames` and `TheysGone.ini` were backed up
outside the repo.

The host INI already had `ControllerClass=PlayerController`,
`PawnClass=BendDefaultPlayerPawn_C`, and `SpectatorJoin=1`. The guest's
`ConnectUrl` was `steam.<host SteamID>`. The host continued into the
`Locate sterile bandages` objective while the Deck stayed at the title menu.
`net-listen` succeeded: `UWorld::Listen returned true` and the host applied
the configured controller class and spectator-join setting.

`net-connect` on the Deck completed the join. The guest intercepted
`LoadMap`, retained its local `BendDefaultPlayerController_C`, and dropped
the streaming RPCs. The host's `net-probe` reported `ClientConnections=1`.
The guest probe showed an open connection with 9 channels and proxies for the
host's `BP_BendGameState_C`, `BP_bike04Drifter_C`, and
`BendDefaultPlayerPawn_C`, as well as the remote `PlayerController` and two
`PlayerState`s. No AI pawn appeared in that probe snapshot; this alone does
not establish whether AI replication works in another area.

The first host `net-possess` request failed with `Unknown debug command`.
Inspection showed that the installed ASI recognized `net-listen` and
`net-connect`, but not `net-possess` or `net-takeover`; the current local
`package/TheysGone.asi` did contain all four strings. After `make build`, the
current `0.3.0-dev` ASI (SHA-256
`6512772bdadfd94ff7f54dc6dbf1e171c82746717b73938ca6449d2f1647fb51`) was
staged and installed on both PCs. The host game was exited before replacing
the loaded binary. The Deck was relaunched with the new ASI; no network
experiment commands have been run in that fresh Deck process.

At that point the host had not been relaunched with the new ASI: the
documented Steam pipe launch and `steam -applaunch 1259420` did not leave a
running game process. No save or game INI had been intentionally edited, and
pre-run backups were kept on both PCs.

### Run A continuation (2026-10-22)

After completing Steam's first-run Input prompt, the host PC launched with the
updated ASI and continued into gameplay. `net-listen` succeeded with
`ControllerClass=PlayerController`, `PawnClass=BendDefaultPlayerPawn_C`, and
`SpectatorJoin=1`. The Deck's `net-connect` succeeded. The host probe showed
one open `SteamNetConnection` with 9 channels; the guest retained its local
`BendDefaultPlayerController_C` and saw proxies for the host GameState, bike,
Deacon pawn, remote `PlayerController`, and two `PlayerState`s.

Host `net-possess` reported success: it spawned
`BendDefaultPlayerPawn_C_1` for the joined `PlayerController` and set that
controller's `Pawn` to the new actor. About three seconds later, the host
process crashed. The Unreal dump
`.../BendGame/Saved/Logs/unreal-v0-2026.10.22-19.54.44.dmp` records
exception `0xc0000005` at `DaysGone.exe+0x779962`, reading address `0x3d8`
from null. This followed possession closely, but the exact faulting code path
has not been resolved.

The Deck's subsequent probe did not show a replicated pawn on the remote
controller (`Pawn=null`), and `net-takeover` returned
`the replicated controller has no pawn yet`. Thus Run A did not reach the
client-control or movement checks and is not a thin-client result. The crash
means the default Bend pawn plus host possession is not viable in this setup
as tested; the generic `Character` fallback was tried in the following
experiments.

### Character pawn fallback (2026-10-22)

To separate the default Bend pawn from possession itself, the host's
`[Research] GuestPawnClass` was set to `Character`. With the host back in
gameplay, `ControllerClass=PlayerController` and `SpectatorJoin=1`,
`net-listen`, the Deck's `net-connect`, and the probes succeeded. The guest
still retained its local `BendDefaultPlayerController_C`; its proxy for the
remote `PlayerController` owned a replicated `Character_0` pawn with role 2.

Host `net-possess` spawned `Character_0` and assigned it to the remote
controller without an immediate host crash. On the Deck,
`net-takeover` acknowledged success, including `ClientRestart` and setting
the view target. The Deck game then crashed within seconds. Its dump
`unreal-v0-2026.10.22-20.19.21.dmp` records exception `0xc0000005` at
`DaysGone.exe+0x264ee02`, reading `0x350` from null. This establishes that
the generic pawn replicated and takeover ran, but not that player control is
stable. Movement, animation, and continued play were not verified.

The Deck's crash reporter was dismissed with **Don't Report** (no report was
submitted); a process check afterward found no Deck game or crash-reporter
process. At the end of this run, the host PC remained in gameplay with its pause
overlay open and still had the fallback `GuestPawnClass=Character` setting.
Run B is recorded below.

### Run B: Bend controller with Character fallback (2026-10-22)

For a controlled comparison, the host used an empty `ControllerClass`
(`BendDefaultPlayerController_C`) while keeping `GuestPawnClass=Character`
and `SpectatorJoin=1`. The host continued into the live `ORWorld` gameplay.
`net-listen` succeeded. The Deck connected with an open `SteamNetConnection`
and 222 channels. The guest probe showed the remote
`BendDefaultPlayerController_C` as a spectator with `Pawn=null`; the host
probe showed the matching remote controller and one client connection.

The in-game guest probe also saw host AI and world actors replicate. Examples
included 30 `SwarmerPawn_C`, 30 `FreakCrowPawn_C`, 20 `CrowPawn_C`,
18 `MarauderPawn_C`, 15 `RatPackPawn_C`, 12 `SurvivorPawn_C`, and 64
`StateMachine_Animal_C` proxies. This establishes actor replication in a
populated gameplay world, not synchronized AI decisions, combat, or mission
state.

Host `net-possess` reported that it spawned `Character_0` and that
`BendDefaultPlayerController_C_1.Pawn` was set to it. Within seconds, the host
crashed and its next debug request received no acknowledgment. The dump
`unreal-v0-2026.10.22-21.05.29.dmp` (modified at 21:20) records
`0xc0000005` at `DaysGone.exe+0x724a46`, a null-based read at `0x1f00`.
The stack contains ASI frames, but the responsible call has not been
identified. The Deck's subsequent probe still showed the remote controller
with `Pawn=null` and no `Character_0` proxy. `net-takeover` was therefore
not attempted in Run B.

The host crash reporter was dismissed with **Don't Report**; no report was
submitted. The host INI was restored from the pre-Run-A backup
(`ControllerClass=PlayerController`, no `GuestPawnClass` override).

### Run B crash-site analysis (2026-10-23)

The dump and matching game executable were copied from the host PC for local,
read-only analysis; no game files or settings were changed on either PC.
Their SHA-256 hashes are:

- Dump: `6eb199bfa3c9b03720eda9b5ffca6e675300c956fe20e8461a04b8c135d30651`
- `DaysGone.exe`: `17c2fca01d566914147a6d4924d038cb1f5a822c2829e4c45862e2bf837730b3`
- The host PC's deployed `TheysGone.asi` matches the local package hash:
  `6512772bdadfd94ff7f54dc6dbf1e171c82746717b73938ca6449d2f1647fb51`

Disassembly confirms the exception at `DaysGone.exe+0x724a46` is the
instruction `mov rbx, [rcx+0x1f00]`; the faulting `RCX` is null. Its caller
at `+0x587c30` loads `RCX` from `[RDX+0x8d8]` immediately before calling that
helper. At the exception, `RDX` is `0x129c3ab00`, so the nested field at
`[RDX+0x8d8]` is the missing pointer. The caller function is itself invoked
from `+0x59a1c0`; neither native function has been identified by name.

This narrows the failure to a native game routine dereferencing a null nested
object after the possession attempt; it does **not** identify that
object as a controller, camera, HUD, or any other specific subsystem. The
minidump includes the relevant stack but not the heap memory for the object
at `0x129c3ab00`, and the inspected executable has no RTTI result identifying
these functions. A speculative guard or initialization would therefore be
unsafe. No new `net-possess` or `net-takeover` test was run; the guest pawn
was never received in Run B.

### Reflection-backed host crash diagnosis (2026-10-23)

The host PC already had `TheysGone-reflection.txt` with native thunk RVAs and
property offsets. It and the existing object dump were copied for offline
analysis, without launching either game. The executable hash matches the
one recorded above.

**Run A: the Bend pawn expects a Bend controller.** Before the fault at
`+0x779962`, the native routine loads the pawn's `Controller` at `+0x380`,
checks it against the class returned by `+0x132f270`, and clears the result
on a failed cast. That class registration contains `ABendPlayerController`.
The faulting instruction then reads `[RBX+0x3d8]` without a null check;
reflection identifies `PlayerController.PlayerCameraManager` at `0x3d8`.
Run A used a plain `PlayerController`, which cannot pass the Bend-controller
cast. The recorded read of address `0x3d8` is consistent with that null
cast result. This is a controller/pawn type mismatch, not evidence that the
native Bend pawn inherently cannot be possessed by a second player.

**Run B: the generic pawn leaves the Bend-specific pawn cache null.**
Reflection identifies `BendPlayerController.MyBendPawn` at `0x8d8` and
`BendPawn.RideVehicleComponent` at `0x1f00`. The fault path reads
`MyBendPawn` and passes it to the helper at `+0x724a40`, which dereferences
`RideVehicleComponent`. Thus the earlier unknown null argument is the
Bend-specific pawn reference, consistent with possessing a generic
`Character` rather than a `BendPawn`.

The caller's subsystem is also identified without RTTI: the virtual
function at `+0x59a1c0` is referenced by vtable slot RVA `0x3eef040`, in the
vtable installed at `+0x57f93a` by constructor `+0x57f920`. Its constructor
thunk is `+0x1271600`; the class-registration routine at `+0x128fb50`
references that thunk and names `ABendAmbientGenerator`. The generator's
routine at `+0x587c30` follows its controller pointer at `+0xcf0`, reads
the controller's `Pawn` at `+0x350`, then its `MyBendPawn` at `+0x8d8`.
The last field is null at the crash. The `0xcf0` field itself is not named
in the reflection dump.

These findings supersede the earlier inability to name the host fault
path. They explain why the two mismatched host combinations are invalid
tests of normal Bend gameplay. Run C below tested the matching pair; its
guest-side navigation failure is analyzed there.

### Run C: matching native controller and native pawn (2026-10-23)

The user approved use of both PCs for this test. Both games were initially
stopped; settings and saves were backed up outside the repo before changes.

Both deployed ASIs still matched
`6512772bdadfd94ff7f54dc6dbf1e171c82746717b73938ca6449d2f1647fb51`.
The host used empty `ControllerClass`, `PawnClass=BendDefaultPlayerPawn_C`,
`GuestPawnClass=BendDefaultPlayerPawn_C`, and `SpectatorJoin=1`.

**Preflight confounder removed:** both original INIs had `Transport=udp`,
with the host PC joining the Deck in the shipped hybrid mode. The initial launch
auto-connected and started a host-save transfer before any engine-network
test. Both games were exited normally, both INIs were temporarily changed
to `Transport=menu`, and both games were relaunched. The actual Run C below
had no shipped hybrid peer traffic or streamed poses. Prior runs did not
explicitly isolate this setting, so their broad behavioral observations
should not be treated as clean native-only comparisons.

**Host possession succeeded.** The host continued into gameplay.
`net-listen` succeeded at 19:18:36. The Deck connected at 19:18:53; the
probes showed the joined Bend controller, 222 channels and populated-world
AI proxies. At 19:19:47, host `net-possess` spawned
`BendDefaultPlayerPawn_C_1` and assigned it to
`BendDefaultPlayerController_C_1`. The Deck received it as
`BendDefaultPlayerPawn_C_2` (local object suffixes differ between PCs).
The host was still responsive at 19:21:03 and the guest at 19:21:23, over
90 seconds after possession. Both controllers reported `StateName=Playing`;
the host connection's ViewTarget was the new pawn. This removes the earlier
host-side possession blocker for the matching native pair in this setup.

**Guest control did not pass.** At 19:21:45, Deck `net-takeover`
acknowledged `SetPlayer`, `ClientRestart`, and the view-target call. A probe
was still acknowledged at 19:21:54, but the Deck crashed before any camera
or movement verification. Its dump
`unreal-v0-2026.10.23-19.16.08.dmp` was modified at 19:22:02 and records
`0xc0000005` at `DaysGone.exe+0x264ee02`, reading address `0x350`.
SHA-256:
`dfa13f2fb5ee36e942a59c447b437c95ec95001af022920861eb5be858057f68`.
The Deck executable matches the host PC's recorded executable hash.

The guest's final probe still showed both replicated Bend pawns as
`Role=1` (simulated proxies), while the remote controller was `Role=2`
and its `PlayerState` was null. These are additional initialization/
ownership concerns, not proof that guest inputs reach the host. The host
remained running after the guest crash. No movement, combat, or stable
guest camera result was established.

**The guest crash is now localized to navigation.** The dump and
disassembly give the path:

- Door initialization at `+0x436f40` contains explicit
  `ABendDoor::...` delegate strings. It reaches the state-change path at
  `+0x440d0c`, then `+0x43f0b0`.
- At `+0x43f168`, the code obtains the class whose registration names
  `UNavArea_ClosedDoor`, then calls `+0x2668380` through the door's link
  component.
- That function checks its world, loads `[world+0x480]` at `+0x26683ca`,
  and tail-calls `+0x264edb0` without checking the loaded pointer.
  Reflection identifies `World.NavigationSystem` at `0x480`.
- The helper derives container pointers from its null argument (`R9=0x328`,
  `R11=0x338`) and faults reading `[R11+0x18]` at `+0x264ee02`.

Thus a door navigation-area update assumes a navigation system exists on
the client. The crash occurs after takeover exposes this path; the evidence
does not establish that the takeover call itself directly causes the null
system. The reflection dump also exposes the config property
`NavigationSystem.bAllowClientSideNavigation`, but its runtime value and
whether enabling it preserves/creates the required system were not tested.

**Cleanup:** the Deck reporter was dismissed with **Don't Report** (no report
submitted). Restored both original INIs, including their UDP settings; hashes
verified. Saves were not edited. No plugin code was deployed during Run C.

### Run D: instrumented native-pair retest (2026-10-23)

`ProbeNetwork` in `src/coop/research.cpp` now records, for each live world,
its `NavigationSystem` pointer and, when present, the subsystem class,
`bAllowClientSideNavigation`, and `MainNavData`. Each player-controller
record also includes `Player`, `MyBendPawn`, pawn role/remote role, owner,
controller, PlayerState, and `RideVehicleComponent`. This should show when
the client world or autonomous ownership is absent across connect,
possession, and takeover. The diagnostic build (`TheysGone.asi` SHA-256
`c87dae666fcc51682022ad0adfdfa3155416e81de69660c815f63e5acd023ed8`)
was deployed for this approved run. Both machines were backed up outside
the repo (`20261023T180214Z-diag-pre`). Their original
UDP transport was temporarily set to `menu` to isolate engine replication;
the host used an empty `ControllerClass`, native Bend pawn classes, and
`SpectatorJoin=1`.

**The guest NavigationSystem disappears during `net-connect`.** Before
connecting, both main `ORWorld` instances had a live `BendNavigationSystem`,
`MainNavData=RecastNavMesh-Default`, and
`bAllowClientSideNavigation=false`. After `net-connect`, the host retained
its system but the Deck's main world reported `NavigationSystem=null`.
Subsequent Deck probes after host possession and guest takeover still
reported null. This pinpoints the loss to the client join/map-transition
path, rather than a system that was absent at process startup. The runtime
value of `bAllowClientSideNavigation` was false on both before the join;
changing it was not part of this run.

**Native pawn replication succeeds, but the guest PlayerState references are
unset.** Host `net-possess` spawned and possessed
`BendDefaultPlayerPawn_C_1`; the Deck received its local
`BendDefaultPlayerPawn_C_2`. On the host, the joined controller and pawn
had `PlayerState_1`. On the Deck, the replicated controller's
`PlayerState` and the pawn's `PlayerState` remained null before and after
takeover. The Deck did have the replicated pawn as `Pawn`/`MyBendPawn`,
with its `RideVehicleComponent`.

Offline reinspection of the saved Deck probe clarifies that the PlayerState
actors themselves did arrive: it reports `PROXY 2 PlayerState role=1`.
Therefore, this is an unresolved controller/pawn-to-PlayerState association,
not evidence that the PlayerState actors were absent. The saved `PawnRole`
and `PawnRemoteRole` fields were printed as `<stale>` because the probe read
these byte properties as object pointers; those two Run D values are invalid.
The follow-up diagnostic build corrected the byte reads, listed each
PlayerState identity and the GameState `PlayerArray`, and logged the world
plus `WorldSettings.bEnableNavigationSystem` immediately before and after
`MovePendingLevel`. It also used the existing `trace-on`/`trace-off` commands
to check whether `OnRep_PlayerState` ran during join.

At 20:09:34, `net-takeover` acknowledged switching the Deck's local player
to the replicated controller and setting the view target. The Deck probe
at 20:09:47 still showed `NavigationSystem=null`; it crashed at
`DaysGone.exe+0x264ee02` at 20:09:52, matching the previously analyzed door
navigation-area update and null dereference (`R9=0x328`, `R11=0x338`).
No camera or movement verification passed. The Deck dump is
`unreal-v0-2026.10.23-20.05.02.dmp`, SHA-256
`9da91af90b3765b7be5ec436f21023c4aedd40029d92ef34b27711770c6967f6`.

The host PC remained responsive through the guest failure, then produced a
separate dump at 20:10:23 with an access violation at
`DaysGone.exe+0x1abfd5d`; its stack includes plugin frames. That host-side
failure has not been localized and should not be conflated with the Deck's
navigation fault. Its dump is SHA-256
`c6c24b7a5cfa2d4b0b087a404d7932358567b4011359b89fa8e9c2c7f4702b58`.
Both crash reporters were dismissed with **Don't Report**; no report was
submitted.

Both games were stopped. Restored both original INIs and `0.3.0-dev` ASIs;
both ASIs again hash to
`6512772bdadfd94ff7f54dc6dbf1e171c82746717b73938ca6449d2f1647fb51`. Saves were
not edited. Dumps, logs and probe files are kept outside the repo.

## Run D follow-up: transition and PlayerState diagnostics (2026-10-23)

This approved diagnostic-only run used the matching Bend controller and pawn
classes, `Transport=menu`, and the updated research ASI
(SHA-256 `a7bcdfb80c0ea0dee9bf51516f811ecf95ce635897e5f5eac68c39fc5feac31d`).
Both PCs were backed up outside the repo
(`20261023T183152Z-diag-followup-pre`). The host
listened in its live world, the Deck joined, and no possession or takeover
command was run.

**The navigation loss is an explicit effect of `MovePendingLevel`.** The Deck
log records the same `/Game/Maps/ORWorld/ORWorld` world, persistent level and
`BendWorldSettings` before and after the call. Before it, `World.NetDriver` is
null and `World.NavigationSystem` is `BendNavigationSystem_0`; afterward the
SteamNetDriver is assigned, `World.NavigationSystem` is null, and
`bEnableNavigationSystem` remains true. The transition is bracketed at
20:45:01.765–20:45:02.340. The join continues with `notify-repointed=0`.

Offline disassembly of the saved build identifies the cause more narrowly:
the engine vtable target at RVA `0x2c036f0` tail-jumps to RVA `0x2c57e80`
with the current `UWorld` and a null second argument. The latter helper reads
the world field at offset `0x480`, calls an internal routine on the old
navigation object when it is present, then writes the supplied pointer to
that field. The reflection dump identifies `UWorld.NavigationSystem` at
offset `0x480`; therefore this call explicitly clears the existing system.
This is not just a later observation that the pointer disappeared. The
old-system callback target is at RVA `0x262faf0`; its exact symbol and
semantics, and the reason this join path passes null, are not established.
`WorldSettings` still
has `bEnableNavigationSystem=true`, and the host does not take this client
transition path. Do not paper over the clear by manually assigning a
NavigationSystem or suppressing door updates. Establish the Bend-fork
lifecycle and safe reconstruction path first.

A second call-graph pass supports a world-transition-cleanup interpretation.
The helper at `0x2c57e80` also has a direct caller at `0x2631456`, which
passes a non-null value returned by a preceding call; that function's
identity and trigger are not recovered. The old-system callback at
`0x262faf0` is also called by routines at `0x2c4c490` and `0x2c4ee20`, both
of which read `UWorld+0x480`. `UEngine::LoadMap` at `0x2c013a0` directly calls
`0x2c4c490`. This is consistent with navigation teardown being part of the
engine's normal world-transition cleanup, with a separate path able to
install a non-null system. The experimental `HookedLoadMap`, however, keeps
the already-running world and persistent level while invoking
`MovePendingLevel`; the live post-call probe confirms no replacement is
installed on this path. This same-world/transition-cleanup mismatch is the
best-supported explanation for the guest's null system, but it does not
prove that retaining or manually reinstalling the old object would be safe.
*Superseded by the offline follow-up below: the non-null caller is stock
`CreateNavigationSystem`, and a stock client clears navigation the same way
unless `bAllowClientSideNavigation` is set.*

**The guest PlayerState mismatch is stable and asymmetric.** At both the
34.80-second and 49.15-second Deck driver probes:

| Deck object | Role / identity | Association |
|---|---|---|
| `PlayerState_0` | local, Role 3, PlayerId 256, guest account | In `GameState.PlayerArray`; linked to local controller/pawn |
| `PlayerState_1` | proxy, Role 1, PlayerId 256, host account | In `GameState.PlayerArray` |
| `PlayerState_2` | proxy, Role 1, PlayerId 257, guest account | Not in `GameState.PlayerArray`; not linked to joined controller |
| joined `BendDefaultPlayerController_C_1` | Role 2, SteamNetConnection | `Player=LocalPlayer`, but `PlayerState=null`, `Pawn=null`, `StateName=Spectating` |

The host's joined controller is associated with its `PlayerState_1` for the
guest account (PlayerId 257), while its Pawn remains null because this run did not
possess one. On the Deck, the local controller and its original PlayerState
remain in place while the joined controller is retained by
`HandleClientPlayer`; the joined controller's `PlayerState` was null both
before and after that handler. Reflection marks `Controller.PlayerState` as a
network property, while `GameState.PlayerArray` has no `net` flag. The trace
ran for 60.2 seconds (20:44:42.873 to 20:45:43.050), recording 812 unique
class/function pairs. It recorded one `PlayerState_2.OnRep_Owner`; it did not
record `PlayerState_2.ReceiveBeginPlay` or `OnRep_PlayerState`. It did record
`PlayerState_1` name/UniqueId RepNotifies and `ReceiveBeginPlay`. These are
ProcessEvent-trace observations, not proof that every native replication or
initialization path was observed. The missing `PlayerArray` entry is an
association/lifecycle question, not evidence that the array itself failed
network serialization; the missing `Controller.PlayerState` remains
unexplained despite that property being networked and the matching proxy
actor being present. *Correction (offline follow-up below): the table's
`PlayerArray` column covers only the Deck's own `BP_BendGameState_C_0`, not
the replicated `BP_BendGameState_C_1`. The missing `ReceiveBeginPlay` is a
trace-aggregation artifact. Only the null `Controller.PlayerState` stands.*

The connection remained open through the post-join probes; neither game
crashed. No camera, movement, enemy behavior, mission progression or
interaction synchronization was tested. The games were closed without
running `net-possess`/`net-takeover`; no save restore or edit was performed.
Restored both PCs' original ASIs and INIs; hashes verified:

| Machine | Original ASI SHA-256 | Original INI SHA-256 |
|---|---|---|
| Host PC | `6512772bdadfd94ff7f54dc6dbf1e171c82746717b73938ca6449d2f1647fb51` | `06244831ebaaa517a736449b64e8dfb7be6892983aa2f56faeb4fcbdf275b1e3` |
| Steam Deck | `6512772bdadfd94ff7f54dc6dbf1e171c82746717b73938ca6449d2f1647fb51` | `01c062dca61049d9d3e15c7b3bc6d3662248c8cfec070bef7bd3aa1d3ee1d2a0` |

Evidence (post-join probes from both PCs, the Deck trace and log, plus the
offline executable and reflection dump) is kept outside the repo.

## Offline follow-up: client navigation lifecycle and probe artifacts (2026-10-23)

This pass used only the saved executable, reflection dump and Run D
follow-up evidence. No PC was touched.

**The non-null installer is stock `UNavigationSystem::CreateNavigationSystem`.**
RVA `0x2631456` is a `.pdata` fragment of the function at `0x26313f0`
(fragments `0x26313f0`, `0x2631408`, `0x2631456`). Its body matches the
UE 4.10 reference path; the runtime metadata observed in Run H reports
`4.11.1-0+UE4`:

1. It reads `GEngine->NavigationSystemClass` (`[0x144d493c0]+0x150`;
   reflection: `Engine.NavigationSystemClass @336`) and checks that it is a
   child of `UNavigationSystem`.
2. It tests bit `0x2` of the class default object at `+0x38`. Reflection
   puts `bAutoCreateNavigationData` and `bAllowClientSideNavigation` at
   `@56`; the latter is a config property (`CPF_Config`).
3. If that bit is clear, it calls `GetNetMode` (`0x2c50300`) and returns
   null when the result is `3` (`NM_Client`).
4. It tests `WorldSettings.bEnableNavigationSystem` (`+0x350`, reflection
   `@848`).
5. It runs `NewObject` (`0x1ac6d30`) and installs the result through
   `0x2c57e80`, the same setter `MovePendingLevel` calls with null.

`CreateNavigationSystem` has two callers. `UWorld::InitWorld` (`0x2c51040`,
call at `0x2c51314`) calls it when `bEnableNavigationSystem` is set.
`UNavigationSystem::InitializeForWorld(World, Mode)` (`0x263ed10`) calls it
only when `World+0x480` is null, then either calls the new system's
`OnWorldInitDone(Mode)` (vtable `+0x258`) or, for mode 2 (editor), jumps to
`0x2632240`.

**`UEngine::LoadMap` uses the stock client order.** Inside `0x2c013a0`:

| Call site | Call |
|---|---|
| `0x2c0288b` | `UWorld::InitWorld` (`0x2c51040`); no net driver yet, so navigation is created |
| `0x2c0289e` | `MovePendingLevel` through engine vtable `+0x4c0`, only with a pending net game; clears navigation |
| `0x2c02b0e` | `UWorld::InitializeActorsForPlay` (`0x2c51c40`) |
| `0x2c02b1d` | `InitializeForWorld(world, 1 = GameMode)` |

So a normal client join also clears the system in `MovePendingLevel`. The
later `InitializeForWorld` recreates it only if the class default object
allows client-side navigation. The probes recorded
`BendNavigationSystem.bAllowClientSideNavigation=false` on both PCs, so
**a full native LoadMap join would also leave the guest with a null
`World.NavigationSystem`**. This behavior is directly observed in this
binary and matches the UE 4.10 reference path; it is not a defect of the
same-world join shim. This corrects the interpretation in the previous
section. The real defect is that Bend's door navigation-area
path (`+0x2668380` → `+0x264edb0`) dereferences the system without a null
check, because Days Gone never ran as a client.

The candidate repair stays inside the engine's own lifecycle:

1. Before or during the join, set `bAllowClientSideNavigation` on the
   configured navigation-system class. That class is `BendNavigationSystem`,
   a subclass of `NavigationSystem`.
2. After `MovePendingLevel`, call `InitializeForWorld(world, GameMode)`,
   as `LoadMap` does.

The same setting would be needed in a full thin-client design. It is
untested. Open questions:

- whether `OnWorldInitDone` on an already-running world registers
  `RecastNavMesh-Default` and the navigation octree correctly;
- the cost of client-side navigation on the Deck;
- whether door and other Bend navigation paths then behave.

The research build now has an opt-in switch for this, `[Research]
ClientNavigation=1`. It is read at `net-*` command time and defaults to 0.
When enabled, the LoadMap hook records the pre-join navigation class, sets
its default object's `bAllowClientSideNavigation`, calls `0x263ed10` with
mode 1 after a prologue check, and logs the resulting
`World.NavigationSystem` and `MainNavData`.

**Two Run D PlayerState findings were probe artifacts.**

- The ProcessEvent trace keys entries by (class, function) and keeps only
  the first object seen. `PlayerState.ReceiveBeginPlay`,
  `OnRep_PlayerName` and `OnRep_UniqueId` each had count 2 but were shown
  "on" `PlayerState_1`. Only `PlayerState_1` and `PlayerState_2` arrived
  during the trace, so those counts most likely include `PlayerState_2`.
  "No `PlayerState_2.ReceiveBeginPlay`" is therefore unsupported.
- The Deck has two GameStates: its own authoritative
  `BP_BendGameState_C_0` (Role 3) and the host's replicated
  `BP_BendGameState_C_1`, which received `ReceiveBeginPlay` and its
  RepNotifies. The probe printed `PlayerArray` only for the first one found
  (`_0`). In the UE 4.10 reference behavior, `AGameState::PostInitializeComponents` makes the
  newest GameState `World->GameState`, and later PlayerStates add themselves
  to that one. Whether `PlayerState_2` is in `_1`'s array was never
  observed.
- Every `Owner=<stale>` line was also a probe bug. `Actor.Owner` is a
  `WeakObjectProperty` (`@148`) and the probe read it as a raw pointer.

**What remains real:** the joined controller's `Controller.PlayerState`
(`@872`, flags include `CPF_Net` and `CPF_RepNotify`; native
`OnRep_PlayerState` at `0x13b6450`) stays null on the Deck, and
`OnRep_PlayerState` never fired for any controller class. The controller
channel itself works. On the joined `BendDefaultPlayerController_C_1` the
trace recorded:

- `ClientSetHUD`, `ClientCapBandwidth`, `ClientEnableNetworkVoice`,
  `ClientVoiceHandshakeComplete` and four `ClientReceiveLocalizedMessage`
  calls, one of them carrying `RelatedPlayerState_1=PlayerState_2`;
- the server RPC `ServerShortTimeout`.

The `PlayerState` string is referenced by `AController`'s replication
registration (`0x2820010`: Pawn and PlayerState, added unconditionally),
`APawn`'s (`0x2a54e40`) and reflection-registration code. No Bend override
that disables the property was found. The cause is still open. Likely
candidates are an unresolved object reference that is never remapped, and
something on the hybrid guest overwriting the field.

**Probe and trace changes (local, not deployed):**

- `ObjectProp` resolves `WeakObjectProperty` values through `ResolveWeak`
  and returns `?` for a dead object.
- `WORLD` lines add `GameState=`.
- Every GameState gets `GAMESTATE_OBJECT` and `PLAYERARRAY <state>[i]`
  lines.
- The trace counts `OnRep_*`, `ReceiveBeginPlay` and `ClientRestart` per
  object.

`make build` produced ASI SHA-256
`acf8e12fa4ac84d81bfd7b0954c08aa85d8a82dd9a71218bf6aceeb9355d9402`.
`make test` and `git diff --check` passed.

## Run E: ClientNavigation join-only run (2026-10-23)

**Setup.** The user approved the run. Both PCs were backed up outside the
repo (`20261023T19460{2,3}Z-clientnav-pre`: ASI, INI, game `Saved`, Proton
`Saved`), and both received ASI `acf8e12f…`.

- Both INIs: `Transport=menu`.
- Host: `ControllerClass=` and `PawnClass=` empty (matching Bend pair),
  `SpectatorJoin=1`.
- Deck: `ClientNavigation=1` added above the existing `ConnectUrl`.

Both games continued the existing save into `ORWorld`.

Sequence:

1. Deck `net-probe` and host `net-probe` (pre-join).
2. Deck `trace-on`, host `net-listen`, Deck `net-connect`.
3. Post-join probes on both.
4. A late Deck probe about three minutes after the join.
5. `trace-off` (191.6 s, 10,923 unique entries).
6. Deck `dump-objects`.

No `net-possess` or `net-takeover` was run.

**Deck join log:**

```
21:54:07.516 LoadMap before MovePendingLevel: navigation=BendNavigationSystem_0 bEnableNavigationSystem=true
21:54:08.099 LoadMap after MovePendingLevel: navigation=null bEnableNavigationSystem=true
21:54:08.100 client navigation: BendNavigationSystem.bAllowClientSideNavigation set to true; running InitializeForWorld(GameMode)
21:54:08.101 client navigation: World.NavigationSystem=BendNavigationSystem_1 class=BendNavigationSystem MainNavData=null
21:54:09.536 HandleClientPlayer before: PC=BendDefaultPlayerController_C_1 Player=null PlayerState=null Pawn=null
21:54:09.538 HandleClientPlayer after: ... Player=LocalPlayer_0 PlayerState=null Pawn=null
```

**Join-only results:**

| Question | Result |
|---|---|
| 1. Navigation after join | `World.NavigationSystem=BendNavigationSystem_1` with `bAllowClientSideNavigation=true` and `MainNavData=null`, still null in the late probe. The Deck ran stably for over three minutes and both games closed without dumps. The census still shows 2 `RecastNavMesh`, 16 `RecastNavMeshDataChunk` and 5 `AbstractNavData` objects, so the nav mesh exists but is not registered with the new system |
| 2. Active GameState and PlayerArray | `World.GameState` is the replicated `BP_BendGameState_C_1` (Role 1). Its `PlayerArray` holds `PlayerState_0`, `PlayerState_1` and `PlayerState_2`. The Deck's own `BP_BendGameState_C_0` (Role 3) holds only `_0` and `_1`. "Absent from PlayerArray" is confirmed as a probe artifact |
| 3. PlayerState linkage | `PlayerState_2` (PlayerId 257, guest account, Role 1) has `Owner=BendDefaultPlayerController_C_1`. Its per-object trace shows `ReceiveBeginPlay`, `OnRep_Owner`, `OnRep_PlayerName` and `OnRep_UniqueId`. The joined controller's `PlayerState` stays null in every probe, and no object received `OnRep_PlayerState`, `OnRep_Pawn` or `ClientRestart`. On the host the same controller has `PlayerState=PlayerState_1` (PlayerId 257) |

`PlayerState_1` (the host persona) reports `Owner=<stale>` because the host's
controller is not replicated to the guest (weak pointer; see the `Actor.Owner`
note in the offline follow-up above).

The trace does capture native RepNotifies: 64
`StateMachine_Animal_C.OnRep_AttachmentReplication` calls, plus GameState
notifies such as `OnRep_MatchState`. So the missing `OnRep_PlayerState` is
not a tracing gap.

**Door crash path with a live navigation system.**
`0x2668380` is a custom nav-link area update: it stores the new area class,
gets the world, then calls `0x264edb0(World.NavigationSystem, link+0x100)`.
`0x264edb0` is the stock `UNavigationSystem::UpdateCustomLink` loop:

- It iterates the TMap at `navsys+0x328` (`AgentToNavDataMap`, 0x38-byte
  pairs, value at `+0x28`).
- For each entry it calls the nav data's `UpdateCustomLink` (vtable
  `+0x790`).

The old fault at `+0x264ee02` was the read of the map's element count from
a null system. With a live system and an empty map, the loop does nothing.
`ClientNavigation=1` therefore removes the takeover crash path even though
`MainNavData` is not registered. Client-side pathfinding stays unavailable.
That is acceptable for a host-authoritative thin client, but any guest UI
that queries the nav mesh (for example a route display) will get no data.

**Replication registration.** `L"PlayerState"` has six references. Three
are UHT property constructors. `0x2820010` is `AController`'s replication
registration. `0x2a54e40` is `APawn::GetLifetimeReplicatedProps`: it calls
the `AActor` super (`0x2639600`), then appends `FLifetimeProperty` entries
with no active-override call. No `DOREPLIFETIME_ACTIVE_OVERRIDE` for
`PlayerState` was found, so the host sends the property. The gap is on the
client: the reference is either never mapped, or mapped and then cleared.
The trace order is consistent with the first case. The controller channel
opens and `HandleClientPlayer` runs before `PlayerState_2` exists, so the
controller's first bunch would carry a reference the client cannot resolve
yet. The binary has no surviving unmapped-reference log strings to confirm
this.

**Cleanup.** Both games closed without crash dumps. Restored both PCs from
the fresh backups; hashes verified (ASI `6512772b…`, INIs host `06244831…`
and Deck `01c062dc…`). Evidence (probes, trace, log, object census,
screenshots) is kept outside the repo.

**New instrumentation (local, not deployed).** While a trace runs:

- The controller passed to `HandleClientPlayer` is watched. Every change of
  its `PlayerState` or `Pawn` is logged as
  `research: watch +<s> <PC>.PlayerState old -> new`.
- The first `OnRep_*`, `ReceiveBeginPlay` or `ClientRestart` on each
  PlayerState or Controller is logged with its trace offset, to give the
  arrival order.

`make build` produced ASI SHA-256
`3b7f0629af8f9d9658b15f277e88e91f1706518a58cbc1459dd6bfaea640a479`.
`make test` (67 Python tests) and `git diff --check` passed.

## Run F: first takeover with ClientNavigation (2026-10-23)

> **Correction (Run G):** both Run F blockers below were misread. The
> trace counts per class and function, not per object, so the "no tick"
> reading was wrong: `BendDefaultPlayerController_C.ReceiveTick` and the
> `InpAxisEvt_*` events ran 2× per frame, which means the joined controller
> ticks too. The SimulatedProxy role came from Bend's empty
> `GetLifetimeReplicatedProps` on character classes, so pawns replicated
> nothing after spawning. See "Run G" below for the real causes and fixes.

**Setup.**

- ASI `3b7f0629…` on both PCs, deployed with `clientnav-deploy.sh`. Backups:
  host `20261023T202031Z-clientnav-pre`, Deck
  `20261023T202032Z-clientnav-pre`.
- Both INIs use `Transport=menu`.
- Host: `ControllerClass=` and `PawnClass=` are empty (Bend's own classes),
  with `SpectatorJoin=1`.
- Deck: `ClientNavigation=1` and `ConnectUrl=steam.<host SteamID>`.
- Both games were launched through the Steam pipe and loaded into gameplay.

**Timeline (local time).**

| Time | Action | Result |
|---|---|---|
| 22:26:30 | pre-probes | both in gameplay |
| 22:26:40 | Deck `trace-on` | |
| 22:26:42 | host `net-listen` | listening |
| 22:26:58 | Deck `net-connect` | same as Run E: `BendNavigationSystem_1` with `MainNavData=null`; `PlayerState_2` OnReps fired; `HandleClientPlayer` ran |
| 22:28:37 | host `net-possess` | spawned `BendDefaultPlayerPawn_C_1`, possessed by `BendPlayerController_C_1` |
| +117.5 s trace | Deck `ClientRestart` on `_C_1` | watcher: `_C_1.Pawn null -> BendDefaultPlayerPawn_C_2`; `PlayerState` unchanged (null) |
| 22:29:55 | Deck `net-takeover` | `SetPlayer`, `ClientRestart` and view target done, **no crash** |
| 22:53:49 | Deck re-takeover (after a baseline check) | view target on the guest Deacon `_C_2`, HUD visible, **no camera or movement response** |

**Result 1: the door-path crash is gone.** With `ClientNavigation=1`, both
PCs stayed up for over 30 minutes through possession, takeover, a revert to
the Deck's own controller and a second takeover. No crash dump was written
on either PC. This confirms the Run D root cause (null
`World.NavigationSystem` on the client) and the fix.

**Result 2: the taken-over controller processes no input.**

- Baseline: after a second `net-takeover` returned the Deck to its own
  controller `_C_0`, mouse and W moved the Deck's own Deacon. Remote input
  delivery works.
- After re-takeover, mouse, W and both gamepad sticks moved neither the
  camera nor the pawn.
- Trace (`rf-deck-trace.txt`, 1083 s; `rf-deck-trace2.txt`, 124 s after
  re-takeover):
  - `_C_1` got `ReceiveBeginPlay`, `ClientSetHUD`, `ClientRestart` ×2,
    `ClientSetViewTarget`, `OnPawnSet`, `ServerAcknowledgePossession`,
    `ServerCheckClientPossessionReliable` and `ServerVerifyViewTarget`.
    The RPC path in both directions works.
  - `_C_1` has **zero `ReceiveTick` and zero `InpAxisEvt`**. `_C_0` keeps
    ticking and evaluating input axes every frame. So the actor tick of the
    joined controller never runs, which means `PlayerTick` and the input
    stack never run either. Input keeps going to `_C_0`.
  - The replicated pawns (`_C_1`, the host's Deacon proxy, and `_C_2`, the
    guest Deacon) also got `ReceiveBeginPlay` but never `ReceiveTick`.
    Their components do tick: `_C_1`'s `VehicleAimAssistTargeter` 2264
    times, and `_C_2`'s CharacterMovement fired `OnLanded` and
    `MovementModeChanged`.
  - The GameState's `OnRep_ReplicatedWorldTimeSeconds` fires about every
    5 s, so property updates on existing actors keep flowing.

**Result 3: the guest pawn arrives as a simulated proxy.**

| Side | Object | Role / RemoteRole | Owner / links |
|---|---|---|---|
| Host | pawn `_C_1` | Authority / AutonomousProxy (2) | Owner `BendPlayerController_C_1`; controller has `PlayerState_1`; connection ViewTarget is this pawn |
| Deck | controller `_C_1` | AutonomousProxy (2) | `Player=LocalPlayer`, `Pawn=_C_2`, `MyBendPawn=_C_2`, `PlayerState=null` |
| Deck | pawn `_C_2` | **SimulatedProxy (1)** / Authority (3) | `PlayerState=null` |

The host marks the pawn autonomous, but the Deck holds it as simulated. In
the UE 4.10 reference implementation, the actor channel sends `Role` as
`ROLE_SimulatedProxy` when the connection is not the net owner (`bNetOwner`),
and swaps the roles on receive. So the host most likely computed
`bNetOwner=false` for this pawn,
for example because `GetNetConnection` or Bend's owner chain did not resolve
to the guest connection at replication time. Even if the actor ticked, a
simulated proxy's CharacterMovement would not send `ServerMove`. This is
the second blocker for thin-client movement. The cause is still unknown.

**Result 4: `Controller.PlayerState` is still null.** The watcher saw no
`PlayerState` transition for `_C_1` during the whole run, and
`OnRep_PlayerState` never fired. The pawn's `PlayerState` is null as well.

**Gotcha: Steam Deck focus and captures.** The first screenshots after
takeover showed other game art. This was the SteamOS idle screensaver with the
Steam overlay focused (`GAMESCOPE_FOCUSED_APP=769`), not a game fault. Gamepad
B (east) restored focus to app 1259420. Before trusting an input test or a
capture, check `DISPLAY=:0 xprop -root | grep FOCUSED_APP` is 1259420 and keep
sending input so the screensaver does not return. Desktop or Gamescope
captures can show another application; use game-owned logs and probes as the
evidence. (This lesson applies to every later Deck entry.)

**Probe note.** `Owner=<stale>` lines in this run are not evidence; see the
`Actor.Owner` weak-pointer note in the offline follow-up and Run G (G7).

**Cleanup.** Both games closed on the first Alt+F4. Restored both PCs from the
`-clientnav-pre` backups; hashes verified. No crash dump was newer than the
backup. Evidence (screenshots, both traces, probes and logs) is kept outside
the repo.

**Conclusion.** The engine path now survives a full guest takeover. A thin
client still has two concrete blockers, both on the guest side of the
possession handoff:

1. Actors created by the net driver (the joined controller and replicated
   pawns) never run their actor tick on the Deck. Without it there is no
   `PlayerTick`, no input processing and no client movement prediction.
2. The guest pawn is a simulated proxy on the Deck instead of autonomous,
   so CharacterMovement would not send moves even when ticking.

## Run G: thin-client movement and look (G1–G21, 2026-10-23/24)

Host PC and Steam Deck guest, both in gameplay, user away with standing
approval. Every run used the `-clientnav-pre` backups; afterwards both PCs
were restored and hashes verified (ASI `6512772b`, INIs `06244831` and
`01c062dc`). Evidence is kept outside the repo.

**Result: a guest can drive a host-owned Deacon through the engine's own
replication.** Walking and turning work with mouse and keyboard. The host
sees the guest's position exactly. The host survives the guest quitting.
The final pipeline:

1. Both: `[Research] PawnReplication=1`. Host: `ClientAuthoritativePosition=1`.
   Guest: `ClientNavigation=1`, `ClientPossess=1`.
2. Host `net-listen`, guest `net-connect`, host `net-possess`, guest
   `net-takeover`.

### Causes found and fixes, in order

| # | Symptom | Cause (offline RE) | Fix |
|---|---|---|---|
| G1 | Guest pawn is SimulatedProxy on the Deck, W moves the Deck's own Deacon | Host-side ownership is correct (`bNetOwner` code in `UActorChannel::ReplicateActor` `0x2831ee0` is stock). Forcing Role=2 on the Deck (`net-auto`) starts the full prediction loop: about 9.5k `ServerMove`, `ServerUpdateCamera` and `ClientAckGoodMove`. The host applies the moves | led to G6/G7 |
| G2–G4 | After takeover keys and mouse do reach the taken-over `_C_1`, but the pawn does not move; `ControlInputVector` accumulates | Bend's CMC consumes input only when `APawn::IsPlayerControlled` (`0x2a57490`) is true, which needs `PlayerState`. The net pawn's `PlayerState` was null | `WalkConsume` (diagnostic only) made it walk on the client |
| G6 | Deck crash in `CancelCurrentPlayerNavigation` (`exe+0x25b199`) | `PC+0x2078` (player navigation object) is created only in `ABendPlayerController::Possess` (`0x799ea0`, vtable `+0x700`), which a replicated client PC never runs | `ClientPossess=1`: run Bend `Possess` on the client during takeover |
| G7 | No pawn `OnRep_*` ever fires, pawn `PlayerState`, `Controller` and `Owner` null | **Bend character classes override `GetLifetimeReplicatedProps` (vtable `+0x198`) with an empty function (`0x1b2340`).** Pawns replicate nothing after spawning. (Bend also made `Actor.Owner` a weak property; `ownerWeak=00000000ffffffff` is null, not stale) | `PawnReplication=1`: restore stock `ACharacter` `0x275b770` on 10 Bend character vtables. The guest pawn then arrives natively as AutonomousProxy with Controller and PlayerState, and walks on the client without forcing |
| G7–G8 | Host's copy of the guest stays at spawn; client is corrected back | Bend moves by animation root motion (`RootMotionFromEverything`), which the server does not reproduce for a remote pawn. `ClientAuthorativePosition` alone is not enough: `ServerMoveHandleClientError` (`0x27a9be0`) only trusts the client when `ServerCheckClientError` (`0x27a97c0`) passes the `MAXPOSITIONERRORSQUARED` check (GNM `@896`, default 3.0) | `ClientAuthoritativePosition=1` sets GNM `+0x390` and raises `MAXPOSITIONERRORSQUARED` to 1e12 |
| G9 | **Movement sync works**: real W on the Deck walks the guest, host copy matches exactly (-28486 → -25554). But the guest always walks +X and the camera does not turn | see G10–G21 | |
| G10–G11 | `_C_1` gets MouseX, but the look value lands on the Deck's old pawn `_C_0` | Pawn look handlers (`0x77c640`, `0x77c6f0`) run from input bindings in `ProcessInputStack` | |
| G12–G14 | Rebuilding `_C_1`'s stack has no effect | Engine `ProcessPlayerInput` (`0x2aa84e0`) uses one **static** stack (`0x4d36680`) shared by all PCs, rebuilt when the PC's dirty byte `+0x5d8` is set. Both local PCs process input every frame, and `_C_0` rebuilt the stack after `_C_1` | |
| G15 | | | Hook Bend `ProcessPlayerInput` (PC vtable `+0xcc0`, `0x79b1c0`) and skip the own PC during a takeover; hook BuildInputStack (`+0xcc8`) to drop other pawns' components |
| G16 | Map, pause, mission and cheat menus missing from the stack | Bend pushes Blueprint input components on the own PC's `CurrentInputStack` (`+0x5c8`/`+0x5d0`). Elements are pointers to a handle whose first qword is `component+0x30` | Adopt all 6 into the taken-over stack (via FWeakObjectPtr constructor `0x1aceb20`) |
| G17–G20 | Guest turns 0.9° per 800 px, host 19.9° | Look sums, input scales, AddYaw and RotationInput sums and camera manager setup are identical. Only the camera's `AddRotationInput` (camera vtable `+0x248`) differs: -0.015 vs -0.348 | |
| G21 | | Bend `UpdateRotation` (`0x7a5f90`) takes the mouse path `0x45d610` (RotationInput × 0.008 × sensitivity → camera `+0x248`) only if gate `0x5b6040` sees look input on **GetPlayerCharacter(0)** (`0x5ad4b0`, the first entry of `World+0x560` PlayerControllerList, pawn `+0x2a78`/`+0x2a7c`). Otherwise it takes the slow camera manager path (vtable `+0x740`). Player 0 was still `_C_0` | `PromoteController`: move the taken-over PC to index 0 of the world's PlayerControllerList (undo restores the own PC). The guest then turns 20.0° per 800 px, walks in the camera direction and the minimap rotates |

### Other observations

- **Host survives the guest leaving.** After the Deck game was closed, the
  host kept running more than 2 minutes, and `net-diag` showed the guest PC
  and pawn cleaned up. The G6 host crash (`exe+0x1abfd5d`, Blueprint VM)
  did not recur once the guest pawn replicated properly.
- **Final roles.** Host: guest pawn Role=3, RemoteRole=2, Controller=`_C_1`
  with `SteamNetConnection_0`. Deck: `_C_2` Role=2 under `_C_1`, which holds
  `LocalPlayer_0`. The Deck's old controller `_C_0` and Deacon stay alive
  but idle.
- **Gamepad does not work for the guest.** The virtual pad (ydotool and
  game-remote) never produces UE `InputKey`/`InputAxis` events on either PC,
  and after the takeover sticks move neither the camera nor the pawn (G21,
  G21 screenshots). Bend most likely reads pads through Steam
  Input (`BendInputManager`, `SteamInputTable`,
  `WasLastInputKeyFromSteamInput`) and routes them to player 0's pawn or
  the own controller. This needs its own investigation.
- **Player 0 dependency.** Bend's camera gate, HUD, missions and many
  Blueprints use `GetPlayerController(0)` or `GetPlayerCharacter(0)`. The
  promotion fixed rotation, but its other effects are not checked yet:
  which pawn the HUD and minimap show, Blueprint actors now resolving
  `_C_1`, and the menus.

### What this means for native co-op

Thin-client movement and look work with five small engine-level patches in
`src/coop/research.cpp`; this is not a full co-op game. Open items for the
native path are summarized in [REPLICATION-HANDOFF.md](REPLICATION-HANDOFF.md).

## Run H: AI spawn probe and host crash (2026-10-24)

**Purpose.** Check whether a host-spawned AI actor and its changing
properties reach the guest over the engine driver, after Run G restored
native pawn replication.

**Setup.**

- Test ASI SHA-256 on both PCs:
  `48664ec6c3e552d92145894e98d46d2a1d23112870ff4d956a4f6703f488e67c`.
- Host settings: `Transport=menu`, `DebugTools=1`, empty
  `ControllerClass` and `PawnClass`, `SpectatorJoin=1`,
  `PawnReplication=1`, `ClientAuthoritativePosition=1`.
- Guest settings: `Transport=menu`, `DebugTools=1`, `ClientNavigation=1`,
  `ClientPossess=1`, `PawnReplication=1`,
  `ConnectUrl=steam.<host SteamID>`.
- Fresh backups were made before deployment:
  host `20261024T043724Z-clientnav-pre`, Deck
  `20261024T043725Z-clientnav-pre`.
- Both games loaded their existing worlds. `begin` succeeded on both and
  blocked saving. The host started `net-listen`; the guest joined with
  `net-connect`.

**Timeline (local time).**

| Time | Action | Result |
|---|---|---|
| 06:43:43 | Host `begin` | Saving blocked; previous state was unblocked |
| 06:45:27 | Guest `begin` | Saving blocked |
| 06:45:29 | Host `net-listen` | `SteamNetDriver` listen succeeded |
| 06:45:35 | Guest `net-connect` | Connection established; guest navigation recreated |
| 06:46:03 | Host `spawn-single` | Spawned `SwarmerPawn_C_30` with a `SwarmerController_C_30` |
| 06:46:06 | Host `status` | Test swarmer health 200 → 160, moved 505.6 cm; Deacon health 121.875 |
| 06:46:12 | Host and guest `net-probe` | Host had 31 replicated `SwarmerPawn_C`; guest reported 30 proxy `SwarmerPawn_C` actors and 222 open channels |
| 06:46:15 | Host debug session ended itself | Owned test actor cleaned up; saving state restored |
| 06:46:18 | Guest log | First `ServerMove` on `BendDefaultPlayerPawn_C_2` |
| 06:46:22 | Host | Access violation; game exited to the crash reporter |

**What this establishes.** The host spawned and simulated a real swarmer:
its health changed and its location advanced. The guest's aggregate proxy
count is consistent with swarmer replication, but `net-probe` groups
objects by class; it did not identify `SwarmerPawn_C_30` on the guest or
record that actor's health/location transitions. The test therefore does
**not** yet prove the spawned actor's AI state or damage replicated. The
guest screenshot showed the host and guest Deacons, but that does not
identify the test enemy.

**Host crash evidence.** The saved
host dump reports `0xc0000005` at
`DaysGone.exe+0x359dc9`, an invalid read from address `0x20d0`. Disassembly
shows the faulting routine iterating a 16-byte delegate array. Reflection
identifies the caller at `0x12e5a60` as
`ABendAmbientGenerator::OnPlayerRespawn(PlayerController)`; it calls the
delegate helper at `0x591930`, which references
`&ABendAmbientGenerator::OnPlayerTeleported`. The helper follows the
ambient generator's cached controller at `+0xcf0`, then reads
`BendPlayerController.MyBendPawn` at `+0x8d8` and forms a delegate address
at `+0x20d0`. The `MyBendPawn` pointer was null, so the helper passed the
invalid address `0x20d0` to the delegate-array routine.

This resolves the immediate null to the same missing Bend-specific pawn
cache identified in Run B. The post-spawn host probe shows the local
controller still had its Bend pawn, while the joined controller was
`StateName=Spectating` with both `Pawn=null` and `MyBendPawn=null`. The
guest-side probe likewise showed an unpossessed controller. Separately, the
host's Deacon health fell from 121.875 to 56.25 after the swarmer spawned.
The debug session auto-ended shortly afterward; the plugin reported
`local pawn: none` 2.5 seconds later, and the host crashed about four seconds
after that.

The call path therefore matches a missing Bend-pawn cache during a
respawn/possession transition, but there are two plausible null-controller
states in this run: the unpossessed guest controller and the host controller
after its pawn disappeared. The timing is consistent with the swarmer
damaging and possibly killing the host, but does not prove that it caused the
crash. The test did not isolate guest join, enemy damage, and player
respawn.

The host crash reporter was dismissed without submitting a report. The
dump, host/guest logs and network probes are kept outside the repo. No Deck
dump was created.

**Engine-version correction.** The host dump's runtime metadata says
`UE4 BendGame` and `4.11.1-0+UE4`. Earlier handoff and project documentation
called the game UE 4.10.1 based on source-pattern comparisons. Treat those
UE 4.10 references as behavioral analogies, not the exact engine version;
all offsets and conclusions remain specific to executable build 19221447.

**Cleanup.** Restored original ASIs and INIs on both PCs; hashes verified.
The host debug session ended on its own after its pawn changed, cleaned its
test actor and restored the save-block state; the guest was closed while its
save block was still active.

## Run I: join-only and protected AI replication (2026-10-24)

**Purpose.** Separate the unpossessed guest controller from the Run H crash,
then test one host-spawned AI with a valid Bend pawn and host damage
protection.

**Setup.**

- Test ASI SHA-256 on both PCs:
  `48664ec6c3e552d92145894e98d46d2a1d23112870ff4d956a4f6703f488e67c`.
- Host: `Transport=menu`, `DebugTools=1`, `HostSave=1`,
  `PawnReplication=1`, empty `ControllerClass` and `PawnClass`,
  `SpectatorJoin=1`, `ClientAuthoritativePosition=1`.
- Deck: `Transport=menu`, `DebugTools=1`, `HostSave=1`,
  `PawnReplication=1`, `ClientNavigation=1`, `ClientPossess=1`,
  `ConnectUrl=steam.<host SteamID>`.
- The host and Deck original INI/ASI files and save folders were backed up
  before deployment. `begin` succeeded on the host but failed on the Deck
  because its gameplay guard did not find a settled local pawn; therefore
  only the host's save block was active.

**Timeline (local time).**

| Time | Action | Result |
|---|---|---|
| 07:11:11 | Host `begin` | Save block active; previous state was unblocked |
| 07:11:17 | Host `net-listen` | SteamNetDriver listen succeeded |
| 07:13:24 | Deck `begin` | Rejected by the local gameplay-pawn guard |
| 07:14:43 | Deck `net-connect` | Connected without reloading the map |
| 07:14:50 | `HandleClientPlayer` | Local Deck player kept its own Bend controller; joined controller remained unpossessed |
| 07:17:17 | Host `net-possess` | Spawned and possessed `BendDefaultPlayerPawn_C_1`; host `Pawn` and `MyBendPawn` became valid |
| 07:18:53 | Host `status` | Debug session active; Deacon health 121.875 |
| 07:18:59 | Host `god-on` | `bCanBeDamaged=0`, measured mask `0x02` |
| 07:19:07 | Host `probe-player` | Native 5-point damage probe left health unchanged at 121.875 |
| 07:19:44 | Host `spawn-single` | Spawned `SwarmerPawn_C_30`, health 200 |
| 07:19:47 | Host status and both probes | Host swarmer health 160, moved 557.2 cm; host/guest connection remained open |
| 07:20:51 | Second host status and both probes | Host health remained 121.875; swarmer moved 724.7 cm total; connection remained open |
| 07:21:16 | Host `end` | Owned test actor destroyed; original damage and save-block state restored |

**Controller and connection result.** The joined host controller remained
unpossessed for about 2 minutes 27 seconds after the Deck joined, without an
AI spawn or crash. After `net-possess`, the host probe showed its controller
in `Playing` with `Pawn` and `MyBendPawn` set. On the Deck, the joined local
controller was also `Playing`; its pawn was an `AutonomousProxy` with
`MyBendPawn` set. Its controller `PlayerState` remained null, although the
pawn referenced the host's `PlayerState_1`. The Steam connection stayed open
through more than six minutes of join, possession, AI spawn, and cleanup.

**AI replication result.** The immediate pre-spawn baseline was 30 host
`SwarmerPawn_C` actors and 15 guest simulated-proxy (`Role=1`) swarmers.
After exactly one isolated host spawn, the host count became 31 and the
guest proxy count became 16; the guest's total replicated class count also
increased by one. The connection's open channel count changed from 73 to 74.
After host cleanup destroyed the owned test actor, host and guest counts
returned to 30 and 15, and open channels returned to 73. This controlled
one-for-one increase and decrease is strong evidence that the spawned host
actor and its destruction reached the guest. The host measured the actor
moving and taking damage, but the current probe aggregates by class. It does
not identify the exact guest-side actor or record its health/location, so
enemy property replication and AI behavior on the guest are **not yet
proven**.

**Crash isolation.** No crash occurred during the join-only period or the
protected AI test; host health remained 121.875. No new dump was created
(the newest host dump remains the Run H dump at 06:46:22; the Deck has no
dump from this run). This makes an unpossessed remote controller alone less
likely to explain Run H. The fact that Run I stayed stable after assigning
`MyBendPawn` and enabling verified protection is consistent with a host
death/respawn transition having contributed to Run H, but it does not prove
that diagnosis.

**Cleanup.** Both games were closed. Restored the exact pre-test save
archives (the host had three changed saves, the Deck two), ASIs
(`6512772b…7fb51`) and INIs; hashes verified.

## Network RPC surface census (offline, 2026-10-23)

Counted from `TheysGone-reflection.txt` (FUNC_Net = 0x40, FUNC_NetServer = 0x200000):

| Package | Functions | Net RPCs | Server RPCs |
|---|---|---|---|
| Engine/other | 3828 | 132 | 50 |
| `/Script/BendGame` | 8182 | 11 | 10 |
| `/Game` Blueprints | 14513 | 0 | 0 |

The 11 BendGame RPCs are listed under "Bend's gameplay is almost entirely single-player code" above; 10 are server RPCs. None of the 14,513 Blueprint functions (missions, interactions, UI, gameplay scripts) are RPCs. This is leftover early-development multiplayer scaffolding (movement, bike, hitscan, weapon equip), not a networked game. With a thin-client guest, almost every guest action other than moving, riding and shooting has no path to the host. The mod would have to add its own guest-to-host channel for interactions, looting, crafting, mission triggers, dialogue and menus, and its own host-to-guest sync for anything not carried by replicated properties.

## Run J: actor-specific movement and health replication (2026-10-24)

**Purpose.** Extend Run I's aggregate swarmer count test with a per-actor
identity, transform, and health comparison between the host and guest.

**Setup.** Both PCs ran the instrumented ASI
(`3eb22864318c65741e177e4ef429e149c6f4a60f19755a350f626c422f5b2dec`).
The host used `HostSave=1`, `PawnReplication=1`, an empty controller/pawn
class override, and `ClientAuthoritativePosition=1`; the Deck used
`HostSave=1`, `PawnReplication=1`, `ClientNavigation=1`, and
`ClientPossess=1`. The host's debug `begin` succeeded and blocked saving.
The Deck's `begin` was rejected by the living, settled gameplay-pawn guard,
though its game process remained running and later joined the host. No Deck
UI input was sent.

**Timeline (local time).**

| Time | Action | Result |
|---|---|---|
| 07:33:44 | Host `begin` | Saving blocked |
| 07:33:48 | Host `net-listen` | `SteamNetDriver` listen succeeded |
| 07:36:04 | Deck `begin` | Rejected by the gameplay-pawn guard |
| 07:36:39 | Deck `net-connect` | Guest connected to the host without a map reload |
| 07:37:11 | Host `net-possess` | Joined controller possessed a matching Bend pawn |
| 07:37:22 | Host `god-on` and `probe-player` | Native damage probe left host health at 121.875 |
| 07:37:51 | Host `spawn-single` | Spawned `SwarmerPawn_C_30` at health 200 |
| 07:38:35 | Host `status` | Swarmer health 160, moved 627.4 cm; host health 121.875 |
| 07:39:32 | Host `probe-enemy` | Swarmer health changed from 160 to 156.25 |
| 07:39:59 | Paired `net-probe` | Host and guest actor-specific state captured after damage |
| 07:40:54 | Host `end` | Owned test actor destroyed; saving and damage state restored |

**Movement result.** In the paired host and Deck probes, the host actor
`SwarmerPawn_C_30` had `Role=3`, `RemoteRole=1`, and
`bReplicateMovement=true`. The corresponding guest object,
`SwarmerPawn_C_60`, had `Role=1`, `RemoteRole=3`, and
`bReplicateMovement=true`, identifying it as a simulated proxy. At the
paired sample, the host location was `(-29917.4, -206043.8, -9916.2)` and
the guest location was `(-29917.0, -206044.0, -9916.0)`, a difference of
about 0.5 cm. Earlier samples also tracked closely while the host swarmer
moved. This is strong evidence that engine actor movement replication is
working for this spawned enemy.

The suffixes are not a cross-peer identity: the Deck already had a different
local `SwarmerPawn_C_30` at `(100, 100, 100)`, while the received host actor
was `_60`. Future comparisons must use the full object identity together
with a controlled spawn delta and transform, not the numeric suffix alone.

**Health and AI limits.** The host-owned swarmer's health fell from 200 to
160 during its first seconds of simulation; the later native host damage
probe reduced it from 160 to 156.25. At the paired sample, the guest proxy
still reported health 200. Its controller was null, as expected for the
simulated proxy; the probe does not establish that guest-side AI behavior,
damage reactions, animations, or death are synchronized. No enemy death was
tested. The result therefore demonstrates movement replication, while
guest health remained stale under this configuration. Other Bend-specific
properties were not independently tested.

**Offline registration lead.** The archived reflection dump lists the class
chain `SwarmerPawn_C : BendSwarmerPawn : BendFreakerPawn : BendAIPawn :
BendPawn`. It marks `BendPawn.Health` with `CPF_Net` (`pflags=0x18001141000225`)
and the class has `OnRep_Health`; the Run J probe also reports five
`CPF_Net` properties declared on `/Script/BendGame.BendPawn`. Thus the
simple explanation that Health lacks the replicated-property flag is not
supported. The current `PawnReplication` patch replaces empty Bend character
vtable entries with stock `ACharacter::GetLifetimeReplicatedProps`, which
chains `APawn` and `AActor`. A leading hypothesis is that the stock function
does not register Bend-specific properties such as Health, despite their
`CPF_Net` flags. `bReplicateHealthToAll` is a non-net bool adjacent to Health
in `BendPawn`. Runs K and L tested both leads: the flag was already true, and
the stock lifetime list omits Health.

The new save-blocked `enemy-health-all-on` command sets this flag only on
live test enemies owned by the host, and the per-actor `net-probe` output
records the flag on each peer. The command is idempotent; `end` destroys the
owned actor, so the experiment does not persist the change.

**Cleanup.** Host `end` destroyed the owned test actor and restored damage
and saving; the post-cleanup guest probe no longer contained the proxy.
Restored the pre-run save archives (three changed files on the host, two on
the Deck), ASIs and INIs; hashes verified.

## Run K: health-all flag is already enabled (2026-10-24)

The user granted control of both PCs for the current away session. Fresh
save/config/plugin backups were verified on each PC before
deploying ASI `248a006143bdaa678946dc31c8e98500dedd051c04f23b8028ad7c8ccedc1dcc`.
Both debug `begin` calls succeeded. The host's native damage-protection probe
kept health at 121.875. Engine join and matching Bend possession succeeded;
no takeover was needed for this property test.

At 17:29:52 the host spawned owned `SwarmerPawn_C_30`. A native damage probe
changed health 200 -> 196.25; the baseline probe later measured 157 while the
guest simulated proxy `_60` stayed at 200. **Both already reported
`bReplicateHealthToAll=true`.** At 17:30:09, `enemy-health-all-on` explicitly
read back `1 -> 1`; another native hit changed host health 157 -> 153.25.
The host probe at 17:30:13 measured 153.25 and the guest probe at 17:30:17
still measured 200. Thus enabling the flag is not sufficient; this was not
a false-to-true transition experiment. The sampled actor moved between the
sequential probes, so their positions are not a new sub-centimeter sync
measurement.

Host `end` destroyed the owned actor and restored damage/saving state; guest
`end` restored its debug session. Restored saves, config, ASIs and INIs on
both PCs; hashes verified. No new crash dumps. Evidence is kept outside the
repo.

Offline disassembly of stock `ACharacter` registration confirms 12-byte
lifetime entries (16-bit RepIndex at +0, condition at +4, RepNotify condition
at +8) and UProperty RepIndex at +0x40. Run L's diagnostic wrapper preserves
stock registration and reports whether Health's RepIndex is present.

The native `OnRep_Health` thunk (`0x12e3a50`) dispatches to vtable `+0xb00`.
Offline reads of the BendPawn, BendSwarmerPawn, and BendPlayerPawn vtables
resolve that slot to the same folded empty `ret` (`0x1b2340`). Thus even a
successful Health property repair must not be described as restored native
damage reactions or death handling. A property callback can execute while
its game-specific virtual implementation does nothing.

## Run L: actual lifetime list excludes Health (2026-10-24)

Both PCs ran the diagnostic wrapper build
`f550962a68e903bf4288da0bcfef548fecef572e66ecc478fa2f3d93897c9d3a`.
It called the original stock Character registration without adding anything.
After engine join, at 17:35:07 on the host and 17:35:07-08 on the guest,
the wrapper observed `Health@8148 RepIndex=22 registered=0`. The swarmer,
player, and other loaded Bend pawn classes each returned 17 lifetime entries:

```text
index:condition = 1:0,7:0,3:0,4:0,0:0,2:0,8:0,6:8,9:0,5:6,
                  10:0,12:0,11:3,17:4,13:4,14:4,15:4
```

This directly confirms the registration gap; it is no longer inferred from
`CPF_Net` flags. Host possession also succeeded. No enemy spawn or damage
was needed. Restored both installs; hashes verified.

The following build (Run M) adds disabled-by-default
`[Research] BendHealthReplication=1` (requires `PawnReplication=1` on both
peers). It preserves every stock entry and adds only Health's reflected
RepIndex with `COND_None` and `REPNOTIFY_OnChanged`, using the engine
allocator if the array must grow. The option freezes when the vtable wrapper
is installed; changing layouts requires restarting both games. Unit tests
cover entry layout, duplicate preservation, no-condition/no-notify-override
values, growth, allocation failure, invalid arrays, and capacity bounds.

## Run M: native Health updates now replicate (2026-10-24)

Both PCs ran ASI
`378d8f851bd8f53870d5b4d76efc5fbfd4d39784f3dbc5551d255f3589e16eab`
with `PawnReplication=1` and `BendHealthReplication=1`, using the previously
verified/restored Run K originals as the pre-run baseline. Both save-blocked
sessions started, host protection was measured (121.875 unchanged), engine
join and matching Bend possession succeeded. Both lifetime logs showed
`stock-count=17 Health@8148 RepIndex=22 stock-registered=0 added=1 total=18`,
with the added entry `22:0`.

The host spawned owned swarmer `_30` at 17:41:01; its identified guest proxy
was `_60`. The host's health settled from 200 to 160. At 17:41:14 a native
hit changed 160 -> 156.25; host and guest probes at 17:41:18 and 17:41:22
both reported 156.25. A second hit at 17:41:30 changed 156.25 -> 152.5;
host and guest probes at 17:41:34 and 17:41:39 both reported 152.5. The
guest ProcessEvent trace recorded **three `SwarmerPawn_C.OnRep_Health` calls
on proxy `_60`** during the spawn-and-two-hit interval.

This verifies host-to-guest Health property updates after spawn, not merely
the initial spawn value. It does not prove guest-to-host damage, enemy death,
animation, appearance, or authoritative interaction/story handling. The
native Health notification virtual remains empty. The separate
`OnRep_LastTakeHitInfo` thunk (`0x12bed60`) also jumps directly to the folded
empty function, so adding that property alone would not restore hit reactions.

Both debug sessions ended and the host-owned enemy was destroyed. Restored
both installs; hashes verified. Logs, paired probes and the guest trace are
kept outside the repo.

## Run N: appearance gap and a guarded lethal probe (2026-10-24)

Both peers used ASI
`76df55d79b4de0192ff78864ab33c47ba9f9045b6960ca215d2d6d2f8402834b`
with Health registration enabled, protected/save-blocked sessions, and a
possessed guest controller. The host spawned owned swarmer `_30` at
17:48:22. Paired probes included mesh and death-state telemetry.

The host swarmer had the assigned `swarmer_horde_c` skeletal mesh and a live
`SwarmerBVim_C` animation instance. The corresponding guest proxy `_60`
had **`SkeletalMesh=null` and `AnimInstance=null`**, despite `bHidden=false`.
Actor movement and Health replication therefore do not produce a rendered,
animated enemy for this bare-spawn case. The host debug spawn assigns its
look locally; that assignment is not automatically transmitted to the proxy.

At 17:48:38 the bounded lethal `ApplyDamage` call reduced host health
160 -> **1**, not zero. The helper correctly returned an error instead of
claiming death. Both probes then showed Health=1, IsAlive=true,
bIsDying=false and bTearOff=false. **This was not a completed enemy-death
test and says nothing decisive about corpse synchronization.**

Offline tracing resolved `TakeDamage` to `0x753480`. Its lethal path has a
specific Health=1 write at `0x7543ce`. One incoming branch tests
`m_bAttemptMeleeDeathBlow` (`+0xc7d`), another tests the actor's
`BendGrappleComponent` (`+0x1aa8`) state through `0x72bf60`. The host trace
contained the swarmer/player grapple kill animation. This supports a
paired-animation death guard as the explanation, not an immortal enemy or
replication failure. Retry with a fresh owned enemy and damage before it
can engage a player; do not patch out the game's death guards.

The reflected `GameplayStatics.ApplyDamage` in this Bend build is void;
there is no `ReturnValue` property. Earlier diagnostic `returned=0` output
was the reflection helper's absent-field default, not an engine damage
return value. The probe now logs only directly measured Health.

Both sessions ended and the owned actor was destroyed. Restored both
installs; hashes verified. Paired probes and host/guest traces are kept
outside the repo.

## Run O: native enemy death reaches the guest, presentation does not (2026-10-24)

ASI `4eb28055b5cb52bed3bcbfd1ee6b29e0e8cac4c3591db46cebe0101b27225bd4`
removed the invalid damage-return diagnostic; lifetime behavior was unchanged
from N. Both peers again used `PawnReplication=1` and
`BendHealthReplication=1`. Host protection was verified at Health=121.875,
saving was blocked on both PCs, and the joined controller possessed its
matching Bend pawn before the enemy experiment.

The host spawned owned swarmer `_30` at 17:56:37.824 and applied lethal
native damage at 17:56:39.352, before a grapple could start. The measured
Health changed **160 -> -320**, and the command verified `IsAlive=false`.
No direct Health write, death-guard patch, or speculative `Die` call was
needed. This distinguishes N's paired-animation guard from the native
death path.

| Measurement | Host owned swarmer `_30` | Guest received proxy `_60` |
|---|---|---|
| Health | -320 | -320 |
| IsAlive | false | false |
| bIsDying | true | **false** |
| bTearOff | true | true |
| Role / RemoteRole | 3 / 1 | 3 / 0 |
| bReplicates | true | false |
| bReplicateMovement | false | false |
| SkeletalMesh | `swarmer_horde_c` | **null** |
| AnimInstance | `SwarmerBVim_C` | **null** |

The guest recorded two `OnRep_Health` calls for `_60`. The final Health
update and native tear-off arrived, after which the guest's role became
authority and its actor stopped replicating, as expected for a torn-off
actor. **That role change does not mean the guest should own combat or
story decisions.** The missing local dying state and absent mesh/animation
prove that property delivery alone is not a complete client death handler
for this **native Unreal pawn proxy**.

At 17:57:24 both debug sessions ended. The host owned swarmer was absent
from the 17:57:28 cleanup probe, but the guest's torn-off `_60` remained.
Once an actor channel has torn off, destroying the host copy does not
automatically clean up its detached native client copy.

### Scope correction: the production enemy visuals were not exercised

Runs N/O used `Transport=menu` to isolate the engine's SteamNetDriver
experiments. The regular co-op enemy visuals are a separate ASI-owned stream:
`DMEN` sends up to four skeletal-mesh paths, and `DPOS` sends host-evaluated
bone poses at 15 Hz. The guest builds non-colliding poseable-mesh replicas;
its `alive` bit gates guest hit requests, and those replicas expire after
1.5 seconds without pose or appearance traffic. The host's native enemy pawn
remains authoritative.

The saved Run O host/guest logs show zero custom pose traffic and contain no
`perf: enemies` counters or `enemies:` replica lifecycle logs. The native
SteamNetDriver's `_60` pawn is not the ASI's poseable visual replica. Thus
the empty native mesh/animation, guest `bIsDying=false`, and surviving
torn-off native proxy are valid findings about the experimental engine
path, but **do not establish a defect in the production `DMEN`/`DPOS` layer**.
The production enemy appearance, death pose and timeout behavior were
unverified live at the time (see the 2026-10-25 live-save fixture).

No visual corpse correctness is claimed. Restored both installs against the
pre-K manifests; hashes verified, and no new crash dump. Evidence
(`runO-host-*`, `runO-deck-*`, including paired death and cleanup probes and
notification traces) is kept outside the repo.

## Steam Deck isolated Run 1 bootstrap (2026-10-24/25, summary)

Both installs were copied to isolated `run1` test directories (excluding
`BendGame/Saved/`), each with its own Proton prefix; each copied executable
matched build SHA-256 `17c2fca0…37730b3`. Test INIs used `Transport=udp`,
port 27777, `SyncEnemies=1`, `Discovery=1`, `DebugTools=1` and `HostSave=1`.
ASIs across these attempts: `4eb28055…`, `832eef44…`, `e3627902…` (UI probe)
and `6315e1de…`. Live installs and saves were not modified, and only
processes started by each run were stopped.

- **Launch.** Direct Proton launches need
  `WINEDLLOVERRIDES=version=native,builtin` (exit 53 without it). On the Deck,
  Proton 11 always reported a failed copy of `amdxcffx64.dll` into the prefix;
  the game ran anyway and the significance is unknown. A direct launch on
  `DISPLAY=:0` (the Steam UI display) exited with X11 `BadWindow`
  (`X_UnmapWindow`); `DISPLAY=:1` created the game window.
- **First host DMEN/DPOS send (2026-10-24).** The guest connected and
  installed host snapshot `81a5efc1` (3 files, 225,546 bytes) under
  `TheysGone-guest-saves`. During the host's opening mission the host log
  reported `perf: enemies streamed=4` with nonzero `DMEN` and `DPOS` send
  rates (peaks 107.9 pose packets/s and 3.4 appearance infos/s;
  `leon_head_standard` and crows). The guest had no `perf: enemies` line and
  no replica application, so this verified host collection/transmission only.
  `DebugBegin` correctly refused the opening-mission state.
- **Captures.** Deck captures repeatedly showed other applications or the
  Steam launch screen instead of the test copy (see the focus gotcha under
  Run F); none is used as evidence.
- **Crash-report upload risk (2026-10-25).** The Deck's game crash reporter
  automatically uploaded a minidump and returned `Discarded=1`, so a report
  was transmitted. `crs-handler.exe` (`Engine/Binaries/ThirdParty/CRS`)
  supports `--no-uploads`, and `BendGame/Saved/Logs/CRS/settings.dat` is a
  Crashpad settings file (magic `CPds`, version 1, options bit 0 = uploads
  enabled). Clearing that bit (Deck `42dd9010…` → `2da42106…`, host
  `d965b633…` → `5edd21ab…`) did not hold: the next launch reset it to 1 and
  started the handler with an upload URL. The working guard was renaming
  `crs-handler.exe` and `crs-uploader.exe` in the isolated copies
  (`.disabled-no-upload-20261025`); the game runs without them (a non-crash
  check only). References: [Crashpad `settings.h`](https://github.com/chromium/crashpad/blob/main/client/settings.h),
  [`settings.cc`](https://github.com/chromium/crashpad/blob/main/client/settings.cc),
  [Days Gone CRS network-blocking guide](https://steamcommunity.com/sharedfiles/filedetails/?id=2490873667).
  A user-level systemd `IPAddressDeny=any` filter was accepted but not
  enforced, so it is not a guard.
- **Empty main menu.** With the mod, the Deck reached `ORWorld` with a
  runtime `UI_MainMenuWidget_C` that had no options (`MaxMenuOptions=0`,
  `mainPageActive=false`) and no gameplay pawn. The only
  `UI_TitleScreenWidget_C` was the collapsed template in
  `Debug_UXTesting_Screen_C`'s `WidgetTree`, not a runtime screen, so the
  UX-testing-screen clue was a dead end. The mod's CO-OP row injection and
  the guarded `continue-title` neither clear nor create menu rows.
- **Steam Linux Runtime 4 requirement.** Proton 11's `toolmanifest.vdf`
  requires AppID 4183110 (Steam Linux Runtime 4), but the isolated launcher
  ran Proton directly. Wrapping it in the runtime's
  `_v2-entry-point --verb=waitforexitandrun --` removed missing
  multimedia-library warnings but did not by itself fix the menu.
- **Initialized-profile requirement.** A no-ASI baseline (same prefix and
  runtime, Desktop Mode) rendered the first-run Initial Setup screen about
  100 s after its 16:15:49 start. Completing Initial Setup and the Game Data
  Collection prompt (the user chose Limited Data) gave the native NEW GAME /
  CHALLENGES / OPTIONS / QUIT TO DESKTOP menu. With that initialized profile
  the mod-enabled run under Runtime 4 showed a real title splash, and
  `ui-probe` at 16:25:55 reported `max=5`, `mainPageActive=true` and first
  label NEW GAME, with CO-OP inserted at row 3. The Deck launcher was then
  changed to enter Runtime 4 (SHA-256
  `dc3a9cf42f9676303ed4b5e2c587ca4e56f41c74462bb503f05250c6e4a96a53`).
- **Startup root cause.** The blocker is resolved operationally by an
  initialized profile plus Steam Linux Runtime 4. The exact first-run
  mechanism is not proven; a candidate is the guest save sandbox being
  enabled before gameplay while pre-adoption wildcard listings enumerate the
  original folder.

### Paired production load and fresh-guest menu failure (2026-10-25)

The persistent Deck launcher directly reproduced the populated mod menu.
The isolated host launcher was also backed up and wrapped in its installed
Steam Linux Runtime 4, as required by Proton Experimental's manifest.
The host's prior ASI was backed up and replaced with the same tested ASI as
the Deck (`6315e1de8a3fae210f9d2ca3ea253f86e88b2602e9d820ee9901bb798e8b17bb`).
Its isolated Saved tree was backed up, and only the isolated CRS handler and
uploader were reversibly disabled. Live installs/saves were not changed.

At 16:31:18 the guest adopted snapshot `8389696f`: four files, 638,649
bytes. Its already-open menu nevertheless remained NEW GAME first with five
options, both at 16:33:33 and after an ordinary Options round-trip at
16:35:01. This is a reproduced production bootstrap failure, not a failed
network transfer. The guarded Continue command was not used on NEW GAME.
The host's Continue-first menu loaded a health-135 pawn and opening cinematic.
Setup-only native cinematic-skip input later exposed the opening bike mission.

For isolated QA setup only, the stopped guest's transferred bundle and
original isolated SaveGames were separately backed up, then the transferred
bundle was copied into that isolated SaveGames. This is a workaround, not a
seamless-joining fix. On restart the production transfer re-adopted snapshot
`94ed2cea` (638,622 bytes). The guest menu then genuinely offered CONTINUE
first, with seven options, at 16:38:11. Guarded Continue loaded a health-135
pawn; the save guard observed the adopted load and released its pins.

**Observed production-path progress:** at 16:39:06 the guest created three
Crow replicas (100 bones each) and a Leon replica (475 bones, three parts).
Its 16:39:08 telemetry reported four replicas, 38.9 pose applications/s and
zero rejected poses; 16:39:13 reported three replicas and 118.9 applications/s.
Replicas subsequently logged expiry. The paired host log reported four
streamed AI pawns and nonzero enemy packet traffic. These are the first
guest production enemy creation/application observations in this run, not
native-engine proxy evidence. They do not assert mesh fidelity, correct
per-entity transforms, damage/death presentation, or timeout semantics.

**Controlled QA remains BLOCKED:** the in-game `begin` fixture gate refused
the host's opening-mission bike state: it requires a settled living pawn on
foot. The mission restriction also remains intact. No guard was bypassed and
no owned-enemy spawn/hit/death test ran. Desktop captures were setup
diagnostics only, not the automated co-op oracle. Both exact test units were
stopped, with no DaysGone.exe left, and paired logs kept outside the repo.

**Refresh investigation:** the runtime reflection dump identifies
`UI_MainMenuWidget_C.RecheckSaves()` with zero parameters. Adoption previously
never requested that native recheck. A candidate change now queues it after
successful adoption, waits for the visible native main page rather than an
open CO-OP submenu, and discards the request once gameplay starts. It retains
the Continue-first guard and the existing native CO-OP insertion logic.
Invocation alone is not proof that Continue appears: validate against the
original fresh isolated SaveGames, without the QA bootstrap workaround.

### Exact-order refresh regressions reject the candidate (2026-10-25)

`make build` and `make test` passed for the first candidate (all 67 Python
tests and the C++ checks). The original isolated guest SaveGames was restored;
its three files had no autosave. Candidate ASI `12f31a1b...` was hash-verified
on both stopped test installs before launch.

**Early adoption, insufficient oracle:** snapshot `06156201` (638,595 bytes)
arrived before the native menu opened. At 16:43:34 the queued recheck ran;
the menu had Continue first and seven rows at 16:43:44. Guarded Continue
loaded the adopted save at 16:44:20, and a health-135 pawn was measured at
16:44:40. The original isolated SaveGames compared byte-identical to its
backup afterward. This proves that ordering can bootstrap the fresh guest,
not that an already-open menu refreshes.

**Exact failure order, FAIL:** with the host stopped, the same original
guest profile opened NEW GAME first with five rows at 16:46:37. The host
then came online and transferred snapshot `46762642` (638,595 bytes).
Adoption completed at 16:47:24; the recheck was invoked immediately afterward.
At 16:47:49, the visible native main menu still had five rows and NEW GAME
first. An Options return did not fix it. No Continue action was attempted.

**Native redraw follow-up, FAIL:** a second built candidate additionally
called the existing `UpdateSelectedPageOptions`/`MainLabelUpdate` redraw
helper and extended `ui-probe` with the cached save flag and pure native
`HasAnySaves()` query. Before the donor started, the guest menu at 16:54:27
had five rows, NEW GAME first, cached=false, queried=false. Snapshot
`70b2ab8e` (638,635 bytes) was adopted at 16:55:06; the recheck/redraw ran.
At 16:55:34, cached=true and queried=true, but the menu still had five rows
and NEW GAME first. Therefore these calls update save availability but do
not reconstruct the already-initialized menu labels/options.

A black setup capture during the second run was resolved by a one-pixel,
non-click pointer motion; the title splash then rendered. The screensaver
reported inactive. This does not reproduce the earlier game startup failure;
do not diagnose a black idle desktop capture as an engine failure alone.

**Persistence and safety:** both exact units were stopped and both isolated
ASIs restored to the pre-experiment `6315e1de...` build, hash-verified. No
DaysGone.exe remained. Both ineffective production refresh candidates were
removed from source, rather than shipping an invocation-only false fix.
Only the diagnostic cached/fresh-query logging remains. The Continue-first
guard, mission fixture gate, original guest saves, and live installs/saves
were preserved. Final source build and all regression tests passed again.

### Late-adoption title menu rebuild: PASS (2026-10-25)

**Where the rows come from:** the reflection dump shows that
`UI_MainMenuWidget_C.MenuOptionNames_Text` is not a class default. At
runtime the default object's array is empty (`num=0 max=0`), while the
instance has capacity 22. Of all the widget's functions, only
`WaitForInput(bool bForced)` holds `K2Node_MakeArray`, `Array_Add`,
`Array_RemoveItem` and a `HasAnySaves` result. That is the menu's own
"press any key" step: it builds the root rows from the game's labels and
keeps CONTINUE / LOAD GAME only when saves exist. `RecheckSaves` and the
redraw helpers never re-run it, which explains both earlier FAILs. A first
candidate that copied rows from the default object was rejected at runtime:
there was nothing to copy, so it logged that and left the menu unchanged.

**Fix:** the false-to-true edge of `SaveGuardAdopted()` marks the title menu
dirty. On the next tick where the native main page is active, the CO-OP
submenu is closed and the first row is not CONTINUE, the mod calls
`RecheckSaves`, confirms `HasAnySaves`, then calls `WaitForInput(bForced=true)`.
It then clears its CO-OP tracking so the existing in-place injection adds
exactly one CO-OP row to the rebuilt array. No labels are invented. The
Continue-first guard is unchanged and now shares one label check.

**Exact-order verification (ASI `e84995de…`, both isolated copies):**
- Guest started alone with the original isolated SaveGames. At 17:26:28 the
  menu showed 5 rows, NEW GAME first, and saves were false
  (cached and queried).
- Host started at 17:26:38. Snapshot `e8d0d67c` (4 files, 638,618 bytes) was
  adopted at 17:27:12.104.
- At 17:27:12.234 the menu was rebuilt:
  `CONTINUE | LOAD GAME | NEW GAME | CHALLENGES | OPTIONS | QUIT TO DESKTOP`,
  then CO-OP was added at row 5 of 7.
- At 17:28:41 the probe showed 7 rows (instance and active page), CONTINUE
  first, saves true. A desktop capture showed one CO-OP row.
- At 17:29:02 the guarded `continue-title` ran the game's Continue. At
  17:29:19 the save guard reported the host save loaded. The guest despawned
  local hostiles (the host owns enemies) and moved to the host. Status
  reported `playerHealth=135`.

Setup input was limited to a pointer nudge and one Enter at the "PRESS ANY
KEY" splash. Assertions come from the in-game log and probe; the capture is
a diagnostic only.

**After the run:** both exact test units are stopped. The guest's real
isolated SaveGames compared identical to
`SaveGames.before-host-bootstrap-20261025`. Logs are kept as
`run1/logs/{guest,host}-saverows-pass-20261025.log`. Both isolated copies
keep the passing ASI; the previous one is `TheysGone.asi.6315e1de`. `ui-probe`
now also lists the instance and default text arrays of the main menu.
`make build` and `make test` (67 tests) passed. Live installs and saves
were not touched.

## Live-save fixture: enemy, story and reconnect QA: PASS (2026-10-25)

**Fixture.** With the user's permission, the isolated host SaveGames on the
host PC was replaced by a copy of the host account's live saves (autosave01–05,
checkpoint, missionstart, profile, slot01, slot02). The previous isolated
opening saves are kept as `run1/SaveGames.host-opening-20261025`, and the
live copy as `run1/SaveGames.live-host-copy-20261025`. The live install and
live saves were only read. Host `continue-title` loaded an on-foot open-world
state (health 121.875), and the `begin` gate accepted it. ASI `e84995de…` ran
on both sides with Transport=udp (production path).

**Join.** The guest adopted snapshot `0d932691` (11 files, 7,718,422 bytes,
about 14 s) before its menu was built. The menu showed CONTINUE first with 7
rows. Guest Continue at 17:53:11 loaded the host save, then teleported to the
partner at 17:53:43 (health 121.875).

**Enemy cycle** (host `begin`, `god-on`, `spawn-single`):
- The host spawned `SwarmerPawn_C_16` (health 200). About 1 s later the guest
  created replica 1 (`swarmer_horde_c`, 136 bones), applying about 35 poses/s
  with 0 rejected.
- Enemy → guest: the host logged "swung at the partner … forwarded 12", and
  the guest logged "host enemy hit you for 12.0" on each hit (health
  121.9 → 73.9).
- Guest → enemy: two guest `hit-enemy` (25 each) arrived as "partner shot
  hit SwarmerPawn_C_16 for 25.0: health 141.2 -> 122.5". The game scales each
  hit to 18.75.
- Host kill (`kill-owned-enemy`): the lethal probe went from 122.5 to −245,
  and the host controller dropped to none. The guest replica stayed, receiving
  the corpse pose (health 0, kPoseAlive cleared). A later guest `hit-enemy` was
  correctly refused ("needs a guest with a live enemy replica").
- Cleanup: host `end` (17:57:52) restored damage and saving and logged
  "streaming 0". The guest expired replica 1 at 17:57:53.656, within the 1.5 s
  timeout. The guest ended at health 73.875 with owned=0.

**Story.** `story-probe` gave the same result on both peers: 3 active jobs
(IND, CA_AZ_01, IZ_CrazyWillies), tracked IND, signature `6071903c` (0 of
203, episode 2). Guest `story-diverge` (17:58:49) changed its local signature
to `8a7410f1`. The guest detected the mismatch, fetched snapshot `6044d8ec`,
adopted it at 17:58:58 and reloaded through `ReloadBecauseOfJobFailure`. The
host save was loaded at 17:59:06, guest writes went to the sandbox, and the
guest teleported back to the host at 17:59:15. Health was 121.875 and the
signature was back to `6071903c`. The whole resync took about 26 s with no input.

**Reconnect.** At 18:00:54 the guest unit was stopped while the host kept
running. The host logged the guest's disconnect and removed the guest
stand-in. A fresh guest launch adopted the host save at 18:02:05 (before the
menu). The menu was `CONTINUE | LOAD GAME | NEW GAME | CHALLENGES | OPTIONS |
CO-OP | QUIT TO DESKTOP`. Continue loaded the host save at 18:02:55 and
teleported to the host at 18:03:08. The host status was streaming the partner
again, with health 121.875 and signature `6071903c`.

**After the run:** all test units were stopped. Logs are kept as
`run1/logs/{host,guest}-fixture-20261025.log` and
`guest-reconnect-20261025.log`. The guest's real isolated SaveGames compared
identical to `run1/SaveGames.before-host-bootstrap-20261025`. The isolated
host SaveGames still holds the live-save fixture.

Not verified here: the visual fidelity of enemy death on the guest (logs
only show the corpse pose streaming until host cleanup; see the next
section). Mission sync and interactions are covered by later sections.

## Enemy pack replication and death-pose observation (2026-10-25)

The same isolated live-save fixture was used on both PCs, with the guest's
SaveGames sandbox enabled. Both peers had the production UDP transport.

**Pack:** host `begin` and `spawn-pack` created eight independently controlled
swarmers at 18:39:06. The guest created replicas 1–8 at 18:39:07; each used
`swarmer_horde_c` with 136 bones, and none was rejected. Host cleanup at
18:39:30 stopped streaming; all eight guest replicas expired at 18:39:32,
within the 1.5 s timeout. During the attack, host health fell from 121.875 to
56.250. This validates replication and cleanup for a pack, not a balanced
eight-enemy encounter; the run exposed high incoming damage and should use a
safer fixture/controlled invulnerability before further combat-load tests.

**Death pose:** a fresh host swarmer spawned at 18:40:43 and was killed by the
host at 18:40:45 (health 200 to −400). The guest created replica 9 with 136
bones at 18:40:44.536. The host continued streaming one dead enemy at about
30 packets/s; the guest applied about 35.5 poses/s with zero rejected. The
diagnostic guest screenshot at 18:41 showed the player and the partner avatar,
but did not clearly show the enemy corpse. Therefore the screenshot is
inconclusive: pose transport after death is verified, but ragdoll/frozen-corpse
visual fidelity is still unverified.

**Cleanup:** host `end` restored its original saving/damage state and removed
the owned enemy. Both isolated game units were stopped. Logs are kept on each
PC as `run1/logs/{host,guest}-pack-20261025.log`; the guest's isolated
SaveGames still compare identical to
`run1/SaveGames.before-host-bootstrap-20261025`.

## Read-only mission discovery and bike-interaction probe (2026-10-25)

`MissionDiscovery=1` was enabled only in the isolated host and guest INIs,
with originals backed up in `run1/TheysGone.{host,guest}.ini.before-mission-discovery-20261025`.
Both configurations were restored to `MissionDiscovery=0` after the test.
The production UDP pair loaded the same fixture; `story-probe` on both still
reported three active jobs and signature `6071903c` (0 of 203 complete,
episode 2).

The host's read-only tracer recorded the `IND` checkpoint actor's
`TRG_PlayerNearCheckpoint` `ActorBeginOverlap` delegate at 18:46:26, along
with `IND_Jobscript_C.UpdateJobObjectiveStatus` and
`UpdateJobObjectiveProgression` signatures. The signature did not change.
This is evidence that the checkpoint trigger and objective functions exist
and execute, not evidence of mission progression or guest synchronization.

The desktop captures showed the host standing beside a bike-like interactable
with an E prompt and the guest nearby. Keyboard E and controller X did not
produce a confirmed host mount; the host remained on foot. The controller
helper changed the visible prompt to an Xbox-style X, and its keyboard W input
moved the guest about 1 m, confirming those input events reached gameplay.

On the guest, `ride-test` spawned the native interact actor and set its label
to `Ride with <host>`. Neither controller X nor keyboard E produced an
`OnInputSucceeded`/`interaction completed` event, even after moving toward the
prompt. The screenshot showed no active interaction hint, so the probe may
still have been outside the actor's interaction range. This is not a pass or
a confirmed defect in the production passenger path. No mission or pickup was
activated.

**Cleanup:** both isolated units were stopped; both INIs show
`MissionDiscovery=0`. Logs are kept as
`run1/logs/{host,guest}-missiontrace-20261025.log`. The guest's isolated
SaveGames still compare identical to the pre-bootstrap backup.

### Follow-up: passenger prompt input trace (2026-10-25, 19:12 CET)

The safe `ride-test` probe was subsequently brought into range. Its screenshot
showed the E prompt, and pressing E logged both `interaction completed` and
`test interaction completed`. This verifies the safe test actor and input path;
it does not verify production passenger boarding.

For the production path, the host mounted its bike and the guest visibly showed
`Ride with <host>` with an E prompt. Pressing E did not produce
`OnInputSucceeded`, `ride prompt: interaction completed`, or a passenger
boarding log. A three-second guest `ProcessEvent` trace recorded 229 unique
calls. The custom actor's `InteractComponentHandler` returned
`CanRecieveFocus=false` 45 times and `GetInteractLocation=(0,0,0)` 47 times.
The same trace showed `BP_BikeRepair_C.CheckCanInteractBike` returning
`CanInteract=false` while `ResettingBike=true`; this may be unrelated and is
not established as the cause. That run did not confirm the guest was inside
the actor's overlap volume. The sampled `CanRecieveFocus=false` and zero
interact location are not by themselves proof of a defect; the controlled
follow-up below observed those values near a successful interaction.

The guest's isolated unit was stopped, its INI still has `MissionDiscovery=0`,
and its isolated SaveGames compare identical to
`run1/SaveGames.before-host-bootstrap-20261025`. The guest log and trace are
archived as `run1/logs/guest-passenger-interaction.log` and
`run1/logs/guest-passenger-interaction-trace-20261025.txt`. The host's debug
save gate was ended, the isolated host unit is inactive, and its log is
archived as `run1/logs/host-passenger-interaction.log`.

### Follow-up: production passenger boarding and bike follow (2026-10-25)

The opening-bike-mission fixture was unsuitable for this test: the host was
mission-gated and the guest remained about 3.3 km away. Both isolated units
were stopped, the known on-foot fixture from
`run1/SaveGames.live-host-copy-20261025` was restored to the isolated host,
and the guest adopted its 11-file, 7,717,801-byte snapshot. Continue loaded
both clients into the same on-foot open-world area; the host's `begin` gate
accepted the state and later restored its original damage/saving settings.

The host mounted the parked bike. The guest spawned the production native
interaction labeled `Ride with <host>`. An initial E press did not
complete because the guest was not yet overlapping the prompt actor. After
moving closer, the guest trace recorded
`BP_BasicInteractBase_C.ReceiveActorBeginOverlapComponent` and
`InteractComponent.InteractGainFocus`. A subsequent single E press recorded
`BP_BasicInteractBase_C.OnInputSucceeded` and
`BP_BasicInteractBase_C.StartTheInteraction`; the guest log at 20:01:05
reported `ride prompt: interaction completed` and
`passenger: boarded using socket SOCKET_PassengerSeat and pelvis bone hips`.
The prompt changed to `Get off`.

For a short follow check, the host received 800 ms of forward input. The guest
continued to track the moving bike and updated its passenger seat transform;
captures from both clients showed the two characters seated on the bike.
Therefore production passenger interaction and short-distance passenger
follow pass in this controlled open-world fixture. This does not establish
long-distance stability, dismount behavior, or mission synchronization.

The successful trace still sampled `CanRecieveFocus=false` and
`GetInteractLocation=(0,0,0)` for the native interaction component. The safe
prompt trace also contained those values before its successful
`OnInputSucceeded`. They are not sufficient to diagnose focus failure without
checking the overlap and input events.

**Cleanup:** both isolated units are inactive. The host's post-run SaveGames
were preserved as `run1/SaveGames.host-passenger-postrun-20261025`, then its
known on-foot fixture was restored. The guest's real isolated SaveGames still
match `run1/SaveGames.before-host-bootstrap-20261025`; its writes remained in
the sandbox. Both INIs retain `MissionDiscovery=0`. Final logs are
`run1/logs/host-passenger-production-pass-20261025.log` and
`run1/logs/guest-passenger-production-pass-20261025.log`; the successful
interaction trace is
`run1/logs/guest-production-passenger-overlap-input-20261025.txt`.

## Live mission lab: observe and drive job transitions (2026-10-26)

**Setup.** Host PC, isolated game copy, live-save fixture (story
signature `6071903c`, episode 2, tracked `IND-JobData` at objective 5,
"Locate sterile bandages"). Saves were backed up to
`SaveGames.host-before-20261026-missionlab` and restored afterwards, because the
game autosaved the changed objective. `[Research] MissionActions=1` was set
only in the isolated INI.

**Live job surface.** `mission-state` found 203 jobs and missions, 3 active
(`CA_AZ_01-JobData`, `IND-JobData`, `IZ_CrazyWillies-JobData`), none complete.
The live managers are the Blueprints `BP_JobManager_C` (base
`BendJobManager`) and `BP_MissionManager_C` (base `BendGameMission`); each job
has a spawned script actor such as `IND_Jobscript_C_0`. The per-job
`JobClassInfo` data asset holds `JobCurrentState`, `CurrentJobObjectiveIndex`,
`StoredRowName` (`IND-ImNoDoctor`) and the objective lists.

**Results.**

| Action (`mission-act`) | Result |
|---|---|
| `TrackAJobOrMission IZ_CrazyWillies-JobData OverrideCurrentTracked=1` | Returned true; tracked job changed immediately; tracking back to `IND-JobData` also worked |
| `DebugCompleteAllJobObjectives IZ_CrazyWillies-JobData` | Returned true, no state change |
| `DebugCompleteAllJobObjectives IND-JobData` | Returned true, no state change: the `Debug*` job functions appear to be compiled out of this shipping build |
| `IND_Jobscript_C.MedBoxObtained` | Objective 5 → 6 immediately, old objective removed from the HUD, new objective "Take sterile bandages to Boozer" with a 440 m waypoint, and the game autosaved |
| `IND_Jobscript_C.UpdateJobObjectiveStatusAndProgression(ObjIdentifier=MedBoxObtained, ObjState=3)` | No state change; `MedBoxObtained` is an event name, not a confirmed objective identifier, so this does not show whether the generic updater works |
| `IND_Jobscript_C.CompleteTheJob` | Returned true; IND changed from active/incomplete/state 2 to inactive/complete/state 4, and tracking moved to `CA_AZ_01-JobData` |

After adding objective indices to the story hash, the host-only live check
reported signature `efeda779` at IND objective 5. Calling
`MedBoxObtained` advanced it to objective 6, triggered a save, and changed the
reported signature to `94993472`. This verifies host-side detection and
persisted-signature capture; the paired save-resync reaction is validated
below.

**Observation limits.** With `mission-watch-on` active through the whole
transition, the ProcessEvent filter logged **zero** mission calls. The
objective chain (`UpdateJobObjectiveStatus`, `UpdateJobObjectiveProgression`,
HUD updates) runs as Blueprint-to-Blueprint calls inside the VM and never
passes through `ProcessEvent`. Only events that native code raises, such as
the checkpoint overlaps seen earlier, are visible there. The 0.5 s state poll
of `CurrentJobObjectiveIndex`, `JobCurrentState` and completion did catch the
change, so polling is the reliable host-side observer.

**Objective IDs and updater follow-up (2026-10-26).** `mission-state` now
reflects the `BendJobObjectives` elements in each active job's current and
default objective arrays. On the saved IND fixture, objective index 5 was
`IND3` in state 1, with earlier objectives in state 2 and the next objective
in state 0. Calling
`IND_Jobscript_C.UpdateJobObjectiveStatusAndProgression(IND3, 2)` advanced
the index from 5 to 6 and set `IND3` to state 2. Repeating the same call left
the index at 6 and reported zero immediate state changes. The first call
changed `autosave01.sav`; the isolated save set was preserved and the
pre-test backup restored byte-for-byte. This verifies the host-side generic
objective updater and repeat-call idempotence for a completed objective. A
paired test subsequently verified guest application for this state-2
objective.

**Host-only non-completed objective-state probe (2026-10-26).** Starting from
the isolated host fixture (`efeda779`), setting the next objective `IND4`
from state 0 to state 1 through `UpdateJobObjectiveStatusAndProgression`
changed the live objective list and autosaved, without changing the then-v2
story signature. That exposed a gap: the guest would see neither a DMOB packet
(which intentionally carries only state 2) nor a story mismatch to request the
new save. Story hashing now includes each active job's current objective IDs,
array indices and state bytes; the DMST version is bumped to 3. On the updated
host, the same state-0-to-1 mutation changed the signature from `fa97ef49` to
`1e6f784a`, confirming host-side detection. A separate call requesting
`ObjState=0` for the current objective `IND3` resulted in `IND3` state 3,
advanced the objective index 5 -> 6, changed the signature to `94993472`, and
autosaved. The updater's input state is therefore not a reliable direct
assignment for every value; direct DMOB application remains limited to the
verified idempotent state 2. The Deck was unreachable during this probe;
paired state-1 save-resync validation was subsequently completed below.
The isolated host ASI, INI and saves were restored and compared with the
pre-test backup.

**Implications for mission sync.**
- `ReadStory` now includes each job's objective index and the
  identifier/index/state of active jobs' current objectives; the host's
  periodic `DMST` packet is version 3 and sequenced (stale or duplicate
  numbers are rejected). A saved transition is caught by the six-second
  reconciliation, which reloads the host save: save-backed, not immediate.
- Job scripts expose the Blueprint-callable primitives
  `UpdateJobObjectiveStatusAndProgression(Name ObjIdentifier, Byte ObjState)`,
  `GoBackToJobObjectiveStage(Int)`, `CompleteTheJob` and `FailTheJob`, plus
  `BP_JobManager_C.StartJobAtID`/`UnlockJobAtID`.
- Opt-in `MissionObjectiveSync=1` (default off, both peers) sends completed
  objectives in sequenced `DMOB` v1 packets every 3 s; the guest applies them
  after host-save adoption with `GetObjectiveState` and the idempotent
  updater, with save resync as the fallback.
- **Host-only packet validation (2026-10-26).** Deployed build
  `7a8e81fb567b606dd34892935add89e173690c099b2db0eba0b5a91c81ee4a72` to the
  isolated host copy and enabled the option only in its test INI. In gameplay,
  the fake peer observed 130 `DMOB` packets across repeated snapshots for
  `IND-JobData`: `IND0`, `IND1`, `IND5`, `IND6` and `IND2`, all state 2. The
  live objective report showed `IND3` state 1 and `IND4` state 0, confirming
  that only completed objectives were sent. This validates host extraction,
  encoding, transmission and observation.
- **Paired guest-application validation (2026-10-26).** With both peers
  configured for UDP, `HostSave=1` and `MissionObjectiveSync=1`, the Deck
  connected to the isolated host and installed its 7,719,152-byte save
  snapshot. Both `story-probe` reports initially matched at `efeda779`. After
  invoking the verified `IND_Jobscript_C.MedBoxObtained` event on the host,
  the host signature advanced to `94993472`; the Deck logged
  `objective sync: applied IND-JobData.IND3 from host`, and its subsequent
  `story-probe` matched at `94993472`. This verifies one state-2 objective
  application after host-save adoption and gameplay readiness. The Deck needed
  to be foregrounded for the large save transfer to make progress. The
  additional objective states still need paired validation; job
  start/completion are covered below. Both game
  processes were stopped; the Deck and isolated host ASIs, INIs, and SaveGames
  directories were restored and compared with their pre-test backups.
- **Paired save-resync fallback validation (2026-10-26).** With
  `MissionObjectiveSync=0`, both peers first matched at `efeda779`. The guest's
  `story-diverge` aid unlocked `IZ_LoggingCampReset-JobData`, changing its
  signature to `ef97a934`; the guest then fetched and loaded the host save and
  returned to `efeda779`. A second run used the host's
  `IND_Jobscript_C.MedBoxObtained` event. The host autosaved signature
  `94993472`; the guest detected its `efeda779` mismatch, fetched the
  7,722,010-byte host snapshot, adopted 11 save files, and reloaded in place.
  The guest's subsequent `story-probe` matched `94993472`, with no DMOB
  application. This verifies fallback for forced divergence and one real host
  objective autosave; broader campaign progression remains untested. Both
  game processes were stopped, and the Deck and host ASIs, INIs, and SaveGames
  were restored and compared with their pre-test backups.
- **Paired job start/completion validation (2026-10-27).** Both peers ran
  `MissionObjectiveSync=1` and matched at `efeda779`. Job start: the host's
  `BP_JobManager_C.StartJobAtID BE_AZ_01-JobData OnlyLoadIfUnlocked=1
  UseSelectionScreen=0` made `BE_AZ_01` active (state 1 -> 2) but did not
  autosave, so the guest correctly stayed on the host's saved signature. The
  next host autosave (`IND_Jobscript_C.MedBoxObtained`) carried both changes,
  signature `1a89e029`. The Deck had already applied `IND3` through DMOB
  (`94993472`), then detected the difference, fetched the 7,735,902-byte
  snapshot, reloaded in place and matched `1a89e029` with `BE_AZ_01` active.
  Job completion: `IND_Jobscript_C.CompleteTheJob` completed IND (state 4,
  signature `6b68ee72`) and opened the native MISSION COMPLETE and trust
  reward screens. While they are open, `BendGameMode.IsSafeForOWSave` returns
  false with `ReasonForFailing=12`, and `BendGameplayStatics.SaveGameOW` and
  `BendQuickSave` do not save. After the host pressed Continue through the
  screens, the game autosaved; the Deck detected `6b68ee72`, fetched the
  7,762,538-byte snapshot, reloaded and matched about 33 s after the host
  save, with IND no longer active. No DMOB errors were logged. So job
  start/completion propagate through save resync at the host's next save,
  not immediately. Both peers were restored and compared with the
  `before-20261027-jobpair` backups.
- **Paired DMST v3 state-1 save-resync validation (2026-10-26).** Both
  isolated copies ran build
  `b12bf60e963a0cc9c69ea8468e271451976fe07ac9ceac3f1910c6b53198dd1a`
  with `HostSave=1` and `MissionObjectiveSync=1`. After host-save adoption
  and job initialization, both peers matched at `fa97ef49`, with `IND4`
  in state 0. The host's
  `IND_Jobscript_C.UpdateJobObjectiveStatusAndProgression - ObjIdentifier=IND4 ObjState=1`
  changed the current objective state to 1 without advancing the index.
  The host autosaved `1e6f784a` at 22:39:43.940; the Deck detected the
  mismatch and requested the new save at 22:39:53.339. It installed the
  7,726,839-byte snapshot (11 files) at 22:40:38.789, invoked the automatic
  in-place reload and logged the host save loaded at 22:40:46.440. Its
  22:41:14.609 probe matched `1e6f784a`, and its current objective array
  confirmed `IND4` state 1. No direct DMOB applications, objective-application
  failures or objective-snapshot failures were logged. This verifies the
  saved state-1 fallback, not direct non-completed objective application or
  broader campaign progression. Both games were stopped, and both isolated
  ASIs, INIs, SaveGames, prior guest sandboxes and debug mailboxes were restored
  and compared with `before-dmst-v3-pair-20261026-2234`. Post-test logs and
  saves remain under that backup's `after` directory.
- **Paired objective failure and reload follow-up (2026-10-26/27).** Both
  peers started at `fa97ef49` with IND active at objective 5 and `IND3` in
  state 1. The host's
  `IND_Jobscript_C.UpdateJobObjectiveStatusAndProgression - ObjIdentifier=IND3 ObjState=0`
  changed `IND3` to failed (state 3), advanced IND to objective 6 and
  autosaved `dc795cfb`. The Deck received snapshot `00790ac5` (7,722,608
  bytes) and adopted all 11 files, but
  `ReloadBecauseOfJobFailure` returned false; its later probe still reported
  `fa97ef49`, objective 5 and `IND3` state 1. On the host,
  `IND_Jobscript_C.GoBackToJobObjectiveStage - TheStage=5` returned true and
  restored objective 5 / `IND3` state 1. This verifies host-side rewind, not a
  paired retry from the failed state.

  A follow-up isolated Deck test staged the host's failure snapshot with the
  host stopped. Continue loaded `dc795cfb`, with IND at objective 6. Invoking
  `ReloadBecauseOfJobFailure` from that loaded state returned false and left
  the signature at `dc795cfb`. This shows the archived host-only snapshot
  contains the failed state and loads through Continue. It does not prove that
  the exact bundle from the paired run was identical, so transfer, sandbox
  routing or save-selection differences are not ruled out.

  For a resync with an expected signature, the guest keeps adopted files
  pinned until a post-load story signature matches host progress; it marks
  progress resynced only after that check. A mismatch is logged and surfaced
  rather than accepted as a successful catch-up. An initial load with no saved
  host signature is still allowed but explicitly logged as unverified. The
  first paired retry failed while both games were running at roughly 0.6
  updates/s during the transfer; this unusual runtime condition is a possible
  contributor, not an established cause. Both isolated copies' ASIs, INIs and
  SaveGames were restored and verified against the
  `before-fail-retry-20261026-2250` backups. Logs and post-test saves are
  archived under those backups' `after-objective-failure-retry/` directories.
- **Paired save-slot trace and verified reload (2026-10-27).** A controlled
  rerun started with both peers matched at `fa97ef49` (IND objective 5,
  `IND3` state 1). The host's
  `IND_Jobscript_C.UpdateJobObjectiveStatusAndProgression - ObjIdentifier=IND3 ObjState=0`
  advanced IND to objective 6, set `IND3` to state 3, and saved `dc795cfb`.
  The host offered snapshot `ae67fc0a` (11 files, 7,725,789 bytes). On the
  Deck, `ReloadBecauseOfJobFailure` was invoked but returned false. The new
  bounded save-open trace showed the game opening the adopted saves from
  `TheysGone-guest-saves`; after enumerating the files, it opened
  `autosave01.sav` again successfully. At 09:19:10 the asynchronous load
  verifier released the pins and confirmed `dc795cfb`. Final host and guest
  probes both reported `dc795cfb`, and both mission reports showed `IND3`
  state 3. Thus the false function return is not proof that the reload failed;
  the post-load signature check is the reliable result. This demonstrates one
  successful paired failure-state resync, but does not explain the earlier
  slow-transfer failure or establish behavior for every transition.

  Both test units were stopped, and each isolated ASI, INI, SaveGames folder
  and guest sandbox was restored and compared with the
  `before-save-slot-trace-20261027-0904` backup. Final logs, mission reports
  and save artifacts remain under `after-save-slot-trace-20261027-0904/` on
  each machine.

**Tooling fixes found during the run.** `Parameters()` printed Blueprint
locals that lie past `ParmsSize`, which read outside the caller's buffer; it
now stops at the parameter block. `mission-act` gained Name parameters, then
static functions on function libraries (run on the class default object, for
example `BendGameplayStatics.SaveGameOW`) and a post-call parameter line that
shows out values such as `ReasonForFailing`.

## Repeated paired resync attempt blocked at guest startup (2026-10-27, 09:41 CET)

**Build and safety.** `make test` passed (C++ checks and 75 Python tests), and
`make build` produced ASI SHA-256
`4886282040b94cb1c23ff0dba9d296aabc35a14937d8526958ccf5bf5b12bf0e`.
The same build was staged on both isolated clients after creating fresh
rollback copies. Crash-handler and uploader executables remained disabled in
the isolated game copies. No live install or save was accessed.

**Host baseline.** The host loaded the isolated on-foot fixture. Its first menu
row was verified as `CONTINUE`; the in-game Continue command loaded story
signature `fa97ef49` with player health 121.875.

**Guest blocker.** The Deck initially launched with `DISPLAY=:1`, where no
usable X11 socket was available and the ASI did not load. That isolated unit
was stopped. Retrying under the active Desktop Mode display (`:0`) loaded the
ASI and connected its UDP client to the host. The guest downloaded and installed snapshot `cd2ee32d` (7,717,293 bytes). One
Return key was sent to each client only as startup setup, not as a QA action.
Afterwards, `ui-probe` still found no runtime main menu or visible Continue
row, and the guest log said to continue from the main menu to load the
snapshot. No host story mutation was issued, so this is **BLOCKED before a
resync trial**, not a failed trial or another pass.

Both game units were stopped. The isolated ASIs, INIs, SaveGames, guest
sandboxes and CRS settings were restored and compared with the fresh backups.
The host and Deck evidence is archived under
`run1/after-repeat-resync-20261027-0935/`; rollback copies are under
`run1/before-repeat-resync-20261027-0935/`.

**Guest-only UI follow-up.** A separate Deck-only launch on `DISPLAY=:0`
loaded the ASI and showed a viewable `Days Gone` window. The configured
`game_remote.py` input helper accepted one bounded Enter setup action, but a
subsequent `ui-probe` still found only the collapsed title-widget template,
with no runtime main menu or visible Continue row. No host was running and no
story action was issued. That diagnostic was also archived, and the Deck
state was restored and hash-checked against the same backup.

## Independent roaming: native streaming path (2026-10-27)

**Question.** What would an independently roaming second player need beyond
the existing close-range partner and enemy streaming?

**Current production limitations established from source.**

- `FinishEnemyScan` selects already-existing host AI near either player's
  absolute position. It does not request level loading, navigation coverage
  or population simulation around the guest.
- `SpawnProxy` creates a `SwarmerPawn_C`, stops its brain and changes its
  faction. `UpdateGuestProxy` moves it to the guest avatar. This provides an
  AI damage/target stand-in, not a second registered Bend player controller
  and pawn.
- Consequently, passing the existing close-range enemy tests must not be
  described as independent-world simulation or unrestricted free roam.

**Read-only native analysis.** Used the host PC's isolated
`run1/game/BendGame/Binaries/Win64/DaysGone.exe` and retained
`precopy-artifacts/TheysGone-reflection.txt`. The executable SHA-256 matched
the supported build:
`17c2fca01d566914147a6d4924d038cb1f5a822c2829e4c45862e2bf837730b3`.
Analysis used bounded `objdump` ranges and direct-call references; no game
was started, no binary patched, and no install, save or configuration changed.
All addresses below are RVAs, not portable engine APIs.

| Boundary | Static evidence |
|---|---|
| Reflected `SetStreamingPosition` | Thunk `0x13c8750` decodes world context, one vector and two bools, then calls `0x5bdd70` at `0x13c888a`. |
| Native position setter `0x5bdd70` | Resolves the world, writes one vector to `world+0x32c`, reads `world+0xc38`, and tail-calls `0x2c65b10`. Reflection identifies `WorldComposition` at decimal 3128 (`0xc38`); the vector's field name is not established. |
| Composition adapter `0x2c65b10` | Forwards the position pointer to `0x2c65b30`, sets the third argument to 1 and forwards two flags. |
| Composition update `0x2c65b30` | Reads the first position, updates cached vectors at `composition+0x28` and `+0x34`, can write `world+0x32c`, and invokes `0x2c60250` with one copied vector and output-array addresses `composition+0x68` and `+0x78`. The inspected body does not consume the incoming third argument as a viewpoint count. |
| Selection `0x2c60250` | Clears both output-array counts, reads world-origin X/Y at `world+0xc18/+0xc1c`, iterates tile records and performs distance-to-bounds comparisons using the input position. The full classification and LOD rules remain to be mapped. |
| Application in `0x2c65b30` | Iterates 24-byte records from both outputs and calls `0x2c5fde0` with different load/visibility flags. Do not infer the complete record layout from stride alone. |
| Normal update entry `0x2c65d30` | Iterates entries obtained through engine helpers, accumulates viewpoints and computes their average for origin rebasing. At `0x2c661b3` it passes a viewpoint count with the array pointer to `0x2c65b30`. Exact player inclusion/filtering still requires runtime confirmation. |

`BendStreamingManager` is not the world-tile manager in this path: its
reflected `StreamManager` member and async asset-load functions identify a
different responsibility. Hooking that class based on its name alone would
not address the traced world-composition boundary.

**Implication, not yet a fix.** The traced path has a single-position
selection bottleneck despite its caller's array/count interface. Calling the
position setter once per player is not a two-focus solution: it overwrites
the world's shared position and recomputes classifications. Calling the
selector twice into the same arrays also discards the first result because
the selector clears their counts. A candidate hook must preserve the host's
focus and origin semantics while producing consistent combined tile/LOD
decisions, not merely alternate positions or increase a radius.

**Follow-up status.** The dual-focus hook and paired free-roam tests below
supersede the static investigation's pending hook and startup-validation
gates. Tile streaming, ground collision, regional AI simulation, reconnect,
and reunion were exercised. Navigation parity, gameplay interactions, stable
enemy identity across unload/reload, and full performance/memory profiling
remain separate validation tasks.

## Free roam: dual-focus streaming and regional enemy authority (2026-10-27)

**Result: PASS for separation, guest-region simulation, reunion handover and
death recovery on the isolated host PC / Steam Deck guest fixture.** Opt-in
with `FreeRoam=1` on both peers; protocol v7 at the time.

### Native dual-focus streaming hook

`streaming_hook.cpp` follows on from the static trace above. Build guard:
executable timestamp `0x68754d46`, plus byte checks at every patched site.

- It detours the composition update `0x2c65b30` and redirects the selector
  call whose return address is `0x2c65c60`.
- The replacement selector runs the original `0x2c60250` once for the host's
  position, then once for the second focus into scratch arrays.
- The two selections are merged per tile:
  - a tile is kept if either focus selects it;
  - the finer LOD wins;
  - the one-shot visibility bit `0x100` is preserved;
  - scratch arrays are grown with `FMemory::Realloc` (`0x19740c0`).
- Tile records live at `composition+0x50`, with count at `+0x58`, stride
  `0xe0` and flags at `+0xa0`. The refresh threshold, about 15 m squared, is
  at `+0x60`.
- The selector takes a position relative to the world origin, so the second
  focus is converted from absolute coordinates each tick.
- The merge logic itself is portable (`streaming.hpp`) and unit-tested.

Host-only measurements (debug focus via `stream-focus`):

| Focus | Extra tiles | Loaded levels | Ground at focus |
|---|---|---|---|
| none | 0 | 127 | — |
| 1.2 km west | 43 | 170 | yes, z≈-2985 |
| towards the map centre | 181 | 295 | yes |
| 3 km east / 1.5 km south (off the map) | 0 / 12 | 127–139 | no |

The host's ticks stayed around 0.6–1 ms with the hook active, and there were
no crashes. Clearing the focus returned the host to 127 levels.

### Regional enemy authority

Streaming tiles alone did not populate AI around a distant focus on the host:
about 1 AI near the focus, versus about 15 in the guest's own world. So when
the players are apart, enemy ownership is split by region:

- **Who decides.** The host decides separation, with hysteresis
  (`streaming::NextSeparated`): over 250 m means separated, under 150 m
  means reunited. A lost partner counts as reunited. The decision goes out as
  state flag `kSeparated` (bit 10).
- **While separated.**
  - The host streams only enemies near itself.
  - The guest releases its local AI suppression, so its own game spawns and
    simulates the enemies around it.
  - The guest streams its nearby enemy appearance and poses to the host using
    the existing `EnemyInfoPacket` and `kPoseEnemy` messages. The host renders
    these as poseable replicas; the guest remains authoritative for their
    simulation and health.
  - The host also keeps the guest's region streamed through the hook, for
    terrain and collision.
- **On reunion.** The guest's existing suppression despawns its local
  hostile AI, the host stops accepting guest-region replicas, and the host's
  streamed enemies take over.

Bug found live: `DecodeState` rejects unknown flag bits, so the first paired
run silently dropped every host packet once `kSeparated` was set. The guest
then timed out after 5 s. The flag mask now includes `kSeparated`, the
protocol version was bumped to 7, and a regression test covers both the
round-trip and rejection of unknown bit 11.

### Paired live evidence (host PC, Steam Deck guest, `move-by` relocation)

- **Separation.**
  - At 1200 m and 401 m the host logged "players separated", and kept 43
    extra tiles around the guest.
  - The guest kept receiving 20–24 packets/s, logged "local AI resumed", and
    had 11–15 controlled AI within 150 m.
- **Guest enemies visible on host.**
  - At 401 m, the guest streamed three local AI pawns (wolves and a deer);
    the host created three poseable replicas with 93-bone rigs and applied
    pose frames continuously (about 70–165 applications/s).
  - A controlled guest-side Swarmer spawn also arrived as a host
    `swarmer_horde_c` replica with 136 bones; the host applied about
    190 pose frames/s for four replicas with no rejected packets.
  - Moving the guest away stopped updates; replicas expired after the
    existing 1.5 s timeout. The guest remained their authority.
- **Guest-region combat is live.** A guest left idle 1.2 km away dropped from
  81.9 to 71.9 health, then to -526 within about 66 s, killed by its own
  region's AI. This happened twice; it is game behaviour, not a sync error.
- **Death recovery.**
  - The host logged "partner gone; the host owns all enemies".
  - The guest respawned with world sync about 15 s later and reconnected.
  - It re-suppressed local AI ("the host owns enemies; despawning local
    hostile AI").
- **Reunion.**
  - Walking back in 200 m steps, the host logged "players reunited (1 m)"
    with no flip back. The guest despawned its local AI the same tick.
  - Health stayed 121.9 throughout the final run, which was without god mode.
- **Test-tool artifacts, not co-op faults.**
  - A single 1200 m jump back into terrain the guest had unloaded made the
    game relocate the pawn about 625 m into the air, and it fell to its death.
  - A complex visibility line trace once placed the pawn on geometry it fell
    through, about 9 m.
  - `move-by` now uses a capsule sweep (simple collision, as the pawn uses);
    the final run placed the pawn instantly with no damage.
  - A navmesh projection attempt never resolved in this build and was
    dropped.

### Open limitations

At the time (protocol v7), guest-region enemies were replicas on the host,
with health, AI and combat guest-authoritative; the gradual handoff below
(protocol v8/v9) changed hit routing. The 600–800 ms frame peaks seen while
paired predate free roam and were later largely removed (see "Paired mission
copies, frame peaks and save reloads"). Open items: see
[COOP-ROADMAP.md](COOP-ROADMAP.md).

## Gradual enemy ownership handoff (2026-10-27)

**Result: PASS for protocol-v8 handoff in both directions on the host PC /
Steam Deck guest fixture.** At the time, protocol v9 added a guest-side host
proxy whose retarget and damage path was not paired-validated (see the
2026-10-30 second attempt, also inconclusive). The design follows Multivoid
and SifuCoop: each enemy has one owner at a time, and ownership changes
gradually.

### Design

- **One owner, symmetric hits.** Entity IDs belong to the owner, so each
  side's replicas are only the other side's enemies. The existing `DMEH` hit
  packet now goes to the owner in either direction; the receiver applies
  `ApplyDamage` to its own pawn after a range check (melee 8 m, shots 150 m).
  The host now also accepts guest enemies whenever the guest has
  `SyncEnemies`, not only while separated.
- **Keeping fights.** Every 0.5 s each side records which of its streamed
  enemies target the guest (`GetCurrentTargetPawn`; on the host, the stand-in).
  On a split the host keeps those engaged within 6 s; on a reunion the guest
  does. Kept enemies are always streamed (sorted first, even beyond 80 m),
  and the guest's suppression skips them and defers the crowd/ambient
  despawn until none remain. Release: death, 6 s without targeting the
  guest, or 180 s (`KeepHandedEnemy`).
- **Stand-in rebuilds.** A host teleport rebuilds the avatar, which removes
  the stand-in for about 5–15 s. The first live split therefore kept 0. The
  fix samples engagement continuously and, while the stand-in is absent,
  judges the fight as of its last presence (`FightingAtHandoff`, up to 30 s);
  the calm clock also waits while it is absent.
- **Guest-side host proxy (protocol v9).** While the guest keeps enemies
  through a reunion, it spawns an AI-less pawn at the host avatar and makes
  nearby guest-owned enemies aware of it. `DMPX` strikes are accepted by
  either player's owner. Experimental at the time; see the 2026-10-30
  second attempt.

### Paired live evidence

- **Reunion (guest keeps).** The guest's `spawn-pack` gave 8 Swarmers; the
  host showed 8 replicas. A host hit at 300 m was rejected ("out of range").
  On reunion (101 m) the guest logged "keeping 8 enemies that are fighting
  the guest", despawned only an uninvolved pooled wolf, and deferred the
  generator despawn. A host hit at 100 m applied (160.0 → 141.2). After
  `cleanup`: 8 releases, "ownership handoff complete", then the deferred
  ambient despawn; host replicas expired 1.6 s later.
- **Split (host keeps).** With 8 host Swarmers on the stand-in, three 100 m
  host moves split the players at 312 m: "keeping 6 enemies that are
  fighting the guest (of 8 sources: 0 dead, 2 not)". The rebuilt stand-in
  appeared 5 s later, and the kept Swarmers kept swinging at it, with strikes
  applied on the guest. Two guest `hit-enemy` calls landed on a kept enemy
  (160.0 → 141.2 → 122.5). Host `cleanup` released all 6 and completed the
  handoff; guest replicas expired about 2 s later.
- **Final-build regroup retest (protocol v8).** On 2026-10-27 the guest moved
  away 299 m, spawned 8 Swarmers, then regrouped. It kept 8 of 10 sources
  (the two uninvolved sources were not kept); cleanup released all 8 and
  completed the handoff. A subsequent host hit was correctly rejected at
  292 m. Test logs are archived outside the repo
  (`handoff-*-regroup-final-20261027.log`).
- **Protocol-v9 guest-proxy attempt (inconclusive).** The guest spawned 8
  debug Swarmers, but they never entered the streamed enemy-source list: the
  host received no replicas, and reunion logged `0 sources`. With no kept
  enemies, the guest-side proxy was not exercised; no retarget or host-damage
  result is claimed. A valid retry must first confirm streamed sources and
  host replicas, then keep the guest alive through reunion and verify both
  proxy targeting and a host health change. Logs are archived outside the
  repo (`v9-guest-proxy-attempt-{host,deck}.log`).

### Limitations

`GetCurrentTargetPawn` may not exist on every controller, and such enemies are
never kept; identities are not carried over at release, so the new owner's
own AI takes over. Open items: see [COOP-ROADMAP.md](COOP-ROADMAP.md).

## Story mission catalog (2026-10-27, single PC, read-only)

Goal: map internal mission and job codes to the names the game shows, as
input for per-mission co-op policy ([STORY-COOP.md](STORY-COOP.md)).

**Static pass.** The pak index of the isolated `run1` copy lists every
`<CODE>n-MissionData` asset, the streamed mission sublevels
(`Maps/ORWorld/Missions/Part<n>/<code>…`, usually with a `<CODE>1_Weather`
level) and per-actor cinematic tracks
(`Cinematics/Act<n>/<mission>/<scene>/<actor>`). Pak contents are
Oodle-compressed, so display names are not readable statically.

**Live pass.** `mission-state` gained a catalog section. Backups of the
isolated prefix's `Saved` tree, the INI and ASI (28 files, SHA-256 list) were
taken in `run1/before-catalog-20261027`, the new ASI
(`e18956ba…7d8a`) was staged only in `run1`, and the copy was started with
`launch-host-test.sh` as a transient unit. At the title menu (no gameplay
pawn, `active=0`) `mission-state` wrote the catalog: 203 jobs and 118
missions. `BendGameMission.GetMissionDataFromIDNew(0..255)` returned all
story missions except `WBB1-MissionData` (found by scanning loaded
`BendMissionData`). The game was stopped, the backup restored, and every
hash verified. Output is archived in `run1/logs/catalog-20261027/`.

Findings:

- `BendMissionData.MissionNameText` and `BendJobData.JobNameText` hold the
  English display names; objective texts name the companions ("Ride With
  Boozer to Crazy Willie's", "Get Lisa to Your Bike", "Protect Rikki").
- `MissionsAct` 2–7 is the story episode (`IND` act 2 matched the story
  signature's episode 2). `GetChapterNameFromEpisode`: 2 "The Drifter
  Road", 3 "Wings Of Angels", 4 "A Camper's Life", 5 "The Forsaken",
  6 "Lost Life", 7 "Rest In Peace". Act 12 is post-credits, 17 the E3 demo,
  18 DLC challenges and test missions.
- Several walkthrough-based inferences were wrong (for example `OBR` is
  *I Say We Head North*, not *We'll Make It Quick*; `BMB` is the latter).
- `BendGameMission.ActiveMissionData` and `CurrentTrackedMission` were null
  at the title menu; reading them during a mission is still untested.

### Active mission and passenger seat (2026-10-27, single PC)

Same backup, staging and restore procedure (`run1/before-policy-20261027`,
28 files). After Enter on the title splash and `continue-title`, the pawn
loaded (`playerHealth=121.875`) with no story mission running:
`status` reported `mission='' missionFlags=0x000`. `mission-act
"BendGameMission.StartMissionFromID - MissionID=3 OnlyLoadIfUnlocked=0"`
restarted the completed *I Say We Head North*; 0.6 s later the log read
`mission: active 'OBR' (policy flags 0x023); passenger seat reserved for the
story`, and the OBR intro cutscene with Boozer was on screen. The game was
stopped, the backup restored and all hashes verified; the log, catalog and
screenshot are in `run1/logs/policy-20261027/`.

Not tested: no partner was connected, so the cleared `kAcceptPassengers` bit,
the hidden prompt and the forced dismount were not observed; the mission was
not completed, so returning to `mission=''` afterwards was not observed.

### Passenger seat with a simulated partner (2026-10-27, single PC)

Backup in `run1/before-onepc-seat-20261027` (28 files); the isolated INI was
pointed at `fake_peer.py --speed 0` on `127.0.0.1:27778`, and the PR build
(`172aefb7…7dac`) staged. The fake peer logged the game's state flags
(`kAcceptPassengers` is bit 32):

| Moment | Game log | Flags seen by the peer | Seat offered |
|---|---|---|---|
| Gameplay, no mission | `mission='' missionFlags=0x000` | 737 | yes |
| `StartMissionFromID` 3 | `mission: active 'OBR' (policy flags 0x023)` | 709 | **no** |
| `FinishEndingMission` | `mission: active 'OBT' (policy flags 0x001)` (chained) | 739 | yes |

During `OBR` the screen showed Deacon riding behind Boozer, the case the
pillion flag describes. `StartEndingMission` alone did not end the mission.
The game was stopped, the backup and INI restored and every hash verified;
the game log, peer log and screenshot are in `run1/logs/onepc-seat-20261027/`.

Not tested: the guest-side reaction (hidden *Ride with* prompt, forced
dismount) needs a second game; it uses the existing permission-removal path.

### Passenger seat in a paired session on one PC (2026-10-28)

Two games on the host PC, each a disposable reflink clone of `run1` in its own
headless gamescope (`tools/lab/lab.sh`, introduced with the test lab). The
host used the desktop Steam client; the guest ran in a bubblewrap sandbox
with a second Steam client signed in to another account. They paired over
loopback UDP (`127.0.0.1:27777` / `27778`) and each showed the other's
avatar. This build adds a guest-side log line when the partner's seat offer
changes.

1. Guest log on connect: `passenger: partner offers the passenger seat`.
2. Host mounted its bike (`E`); the guest saw **Ride with Host**, held F9 and
   logged `passenger: boarded using socket SOCKET_PassengerSeat`.
3. Host `StartMissionFromID` 3 (*I Say We Head North*): host
   `09:13:01.942 mission: active 'OBR' (policy flags 0x023)`; guest
   `09:13:01.944 passenger: partner no longer offers the passenger seat` and
   `09:13:01.951 passenger: dismounted; native body, movement, collision, and
   saving restored`. The dismount followed the withdrawn offer, before any
   cutscene moved the host.
4. Guest F9 during the mission: `passenger: boarding prerequisites not
   satisfied`.

**Side finding: the guest reloads the host's mission-start save.** Starting
the mission made the host write its mission-start save (`story: saved
progress aace70ce`). Six seconds later the guest logged `host progress
aace70ce differs from ours fa97ef49; fetching the host's save`, reloaded,
and reported `mission: active 'OBR'`. *Correction (2026-10-29):* the
screenshots taken as the guest's were captures of the host, and later paired
runs showed that the mission does not start on the guest (no mission script;
its start fade stays black). There is no second copy of the mission. Logs and
screenshots are archived outside the repo (`<archive>/paired-seat-20261028/`).

Not tested: a mission whose pillion character joins mid-mission (Lisa in
`RBC`), the guest-side policy (the guest applied the same flags, but only the
driver's matter for the seat).

## Stealth missions and the partner stand-in (2026-10-28, paired, one PC)

Question: does the host's hidden partner stand-in alert enemies in stealth
missions? Every 0.5 s the host calls `SetAwarenessOfPawn(stand-in, 1)` and
`SetFactionValueOfPawn` for streamed hostile AI within 40 m that is nearer
the partner than the host, whether or not anyone has been seen.

Setup: lab pair (host on the desktop Steam, guest in the sandbox Steam
account, UDP over loopback), fresh clones per run. The host started `AON1`
(*I Kept My Name*, Ripper compound, detection fails the checkpoint) with
`StartMissionFromID 59`; the guest followed through the mission-start save
and stood about 3 m from the host. Neither player moved after the escape
began.

| Run | Build | Rippers streamed | Reports with Rippers targeting the partner |
|---|---|---|---|
| Control | main (`0192238c…a933`) | 13 | 10 of 10, each `13 living streamed enemies target the partner 9, the local player 0` |
| Fix | stealth rule (`c8c59a14…2caa`) | 13 | 0 of 9, each `target the partner 0`, with `225 awareness updates for the partner withheld from enemies that are not fighting` |

In the control run nine Rippers hunted the invisible stand-in for over a
minute although nobody had been seen. Within that window neither player left
the starting room, and the host's checkpoint did not reload. With the fix,
enemies in `kMissionStealth` missions are only told about the stand-in once
they already have a target. Logs and screenshots are archived outside the
repo (`<archive>/stealth-control-20261028/`, `<archive>/stealth-fix-20261028/`).

Not tested: a full mission failure caused by the stand-in (the control run
shows alerted enemies, not the fail screen); enemies that are already
fighting picking the partner as a target in a stealth mission; the enemies'
native sight of the stand-in, which the fix does not change.

### Partner avatar during mission cutscenes (2026-10-28, paired, one PC; inconclusive)

INCONCLUSIVE: all twelve intro-cutscene screenshot pairs captured the host
(see "Lab corrections" below), so they say nothing about the guest's view.
The mod does not hide the partner avatar during cinematics; whether it shows
in a cutscene remains untested.

## Paired mission copies, frame peaks and save reloads (2026-10-28/29, paired, one PC)

Lab pair (host on the desktop Steam account, guest in the sandboxed second
account, UDP over loopback). The host started `OBR` with
`StartMissionFromID 3`; the guest followed through the mission-start save.
With `DebugTools=1` both games logged `mission progress:` lines (checkpoint
and objective states). Ten screenshot pairs were taken 45 s apart.

**Full object scans froze the game.** A first version of the progress log
fell back to `FindInstance("BendMissionScriptActor")`, a scan of every
object, once a second whenever `ActiveMissionScript` was empty. On the build
before #15 the host's tick mean went from 4–7 ms to about 1.9 s (0.5
updates/s) as soon as the mission started, and the mission never ran: one
full scan, with `IsLive` → `Readable` → `VirtualQuery` per object, cost about
1.8–2 s under Proton. The fallback was removed.

**Frame peaks (gap 4).** The same logs showed a recurring 610–645 ms peak in
almost every 5 s window (45 of 55 windows in the 2026-10-28 passenger run).
With #15 (`ReadableRegionCache`) and #17 the 2026-10-29 run had one window at
or above 500 ms out of 120 on the host and none of 107 on the guest; most
peaked below 100 ms.

**Endless reload of an adopted save.** In one run the guest adopted the
host's mission-start save, but each in-place `ReloadBecauseOfJobFailure`
loaded its older checkpoint (`fa97ef49`, not the host's `aace70ce`), and it
retried every 30 s for 20 minutes. With the new limit the guest logged
`3 in-place reloads did not load the host's progress; stopped retrying`.

**Mission copies (2026-10-29 run, current build).** The host's `OBR` ran
on its own: `Obj_1` 0 → 1 → 2 and `Obj_2` 0 → 1 within two minutes, ending
at the roadblock. The guest verified the host's progress `aace70ce` and
reported `mission: active 'OBR'` 18 s after the host, and its mod ticked at
about 60/s, but every guest screenshot was the same black frame and the
guest logged no mission progress (`ActiveMissionScript` empty). The two
copies were therefore not in step; why the guest stays black is not yet
known.

**Lab corrections.** Until this run, the lab's guest screenshots were
captures of the host (`gamescopectl` reads `GAMESCOPE_WAYLAND_DISPLAY`), and
after a reboot the desktop Steam client signed in to the sandbox's account,
which left the host without Continue; both are fixed in `tools/lab/lab.sh`.
Logs and screenshots are archived outside the repo (`sync-20261028-1139/`,
`sync-20261029-2054/`, `reload-loop-20261028/`).

## Partner on the minimap and world map (2026-10-29, paired, one PC)

`minimap-probe` lists every `MiniMapLocationComponent`. The minimap and map
draw `MapLocation` objects (`MapLocationMgr.GetMiniMapLocations`), one per
registered component, using `MapLocation.IconTexture()` and `Position()`.
The player's bike uses icon `MAP_bike`, location type 1, display rule 0.

The partner marker is a spawned native `MapLocationActor`, configured before
`FinishSpawningActor`: icon `RADAR_player_on_foot` (found loaded), location
type 1, display rule 0, edge display 1. Its `MapLocation` was registered and
in the minimap list (`displayOnMinimap=1`, `IconTexture()` correct), but
`Position()` stayed (0,0,0): the actor has no root component, so moving it
does nothing and `bGetPositionFromOwner` reads the origin. With that flag off
and the component's `Position` written every tick, `Position()` followed the
partner on both games (guest marker at the host's exact coordinates).

- World map, partners 280 m apart: the host's map showed the partner icon (a
  white-and-black triangle) at the partner's offset.
- Minimap, partners 35 m apart: with `PartnerMarker=1` the host's minimap
  showed an extra triangle at the partner's offset; with `PartnerMarker=0`
  it did not. On the guest the host's marker overlapped the guest's own bike
  icon, so it was not separately visible.
- The colour tint (`MinimapColor`, `MarkerColor`) is not applied to this
  icon; the partner triangle resembles the game's player icons.

Archives (`map-*`, `mm-off-*`, `mm-on-*`) are kept outside the repo.

## Host's story companion on the guest (2026-10-29/30, paired, one PC)

The host's enemy scan now also streams pawns whose controller is a
`BendBuddyController` (`BuddyController_C`; Boozer is `BoozerPawn_C`, a
`BendAIPawn`), marked friendly in `DMEN` (version 2, `flags` bit 0). Friendly
sources are excluded from the stand-in aggro loop, the 15 s target report,
ownership handoff and incoming guest hits; friendly replicas are skipped by
the guest's shot and melee tests.

In `OBR` the host logged `streaming companion BoozerPawn_C_0` once Boozer
spawned after the intro. The guest's first replica attempt failed because
Boozer's meshes are not loaded on a game that does not run the mission
(`replica 1 skipped; .../boozer_up_thermal_burned_full... is not loaded`).
The guest now requests missing meshes through
`BendStreamingManager.Get().RequestAsyncLoadBPCallback(TAssetPtr, delegate)`
(TAssetPtr: weak pointer, tag, path string; 32 bytes) and rebuilds a replica
that was built without some parts once they have loaded: `replica 1
boozer_head_boozer with 486 bones and 1 parts; rebuilt once the rest has
loaded`, then `... and 3 parts` 5 s later.

With the guest teleported to the host (F7) and its camera turned to the
replica (`look-companion`), the guest's screenshot showed a Boozer-shaped
figure walking 12 m away at Boozer's position; the host's screenshot showed
Boozer at the same spot. The replica rendered grey and blue, not textured.

Materials and Boozer's bike: see "Companion materials on the guest" and
"Companion vehicles on the guest" below. Archives (`buddy-*`) are kept outside
the repo.

## Time and weather during host missions (2026-10-29, paired, one PC)

Time and weather sync used to pause while either game was in a mission. The
guest never runs the host's mission, so the host now keeps sending during
missions (pausing only for cinematics), and the guest applies them unless it
runs a mission script or a cinematic itself. During `OBR` the guest went to
the host's 12.5 h. The mission's fog did not follow: the host's
`WorldWeather` stayed 0 (clear); the fog comes from the mission's weather
sublevel (`OBR1_Weather`), which is streamed on the guest in "Host mission
weather on the guest" below.

## Protocol-v9 guest-side proxy, second attempt (2026-10-30, paired, one PC; inconclusive)

Lab pair with `FreeRoam=1` on both games. The guest moved about 350 m away
(both logged `regions split`), started a debug session, enabled god mode and
spawned a pack: this time the guest streamed its 8 swarmers (`streaming 8
local AI pawns`) and the host showed 8 replicas, which the first attempt never
reached. Moving the guest back to about 26 m from the host merged the regions;
the guest created its stand-in for the host (`partner stand-in
SwarmerPawn_C_24 ...`). Over the next three minutes, with the guest moved
35 m and then about 146 m from the host (still merged), every 15 s report
read `8 living streamed enemies target the partner 0, the local player 8`,
and the host's health stayed 121.875.

Likely reason: the pack consists of debug-spawned pawns owned by the guest's
debug session, which may stay locked on the local player, so the stand-in is
never chosen. A valid test needs naturally spawned enemies that the guest owns
and that are fighting it at the regroup. Logs are archived outside the repo
(`v9proxy-20261030/`).

## Host mission on the guest, flashbacks and a marker crash (2026-10-30, paired, one PC)

Protocol v10 adds one byte to the state packet: the `MissionID` of the story
mission the sender's game runs (0 without a running mission script). Each
game resolves the partner's mission with its own
`BendGameMission.GetMissionDataFromIDNew` and applies the policy table.
While either game runs a flashback mission (`kMissionFlashback`), the partner
avatar and its map marker are hidden.

The host started `WNG` (*What's a Nice Girl*, ID 9, a flashback) with
`StartMissionFromID`:

- The first run crashed the host's game about 2 s after the start, right
  after the host travelled into the flashback and the mod destroyed the
  partner's map marker (`map marker: partner marker removed (partner
  unavailable)`). A control run with the previous build, and a run of the new
  build with `PartnerMarker=0` on the host, did not crash. The marker is now
  hidden with `SetVisibility(false, false)` and kept until the world goes.
- With that change and the marker on: no crash. The host logged `mission:
  active 'WNG'` and `avatar: partner hidden (flashback mission)`; the guest
  logged `mission: partner runs 'WNG' (id 9, policy flags 0x042)`, lifted
  the stuck fade, and hid the host's avatar.

Not tested: that the hidden avatar shows again after the flashback ends.

## Host mission weather on the guest (2026-10-30, paired, one PC)

`level-probe --arg obr` during the host's `OBR` showed the same
`LevelStreamingKismet` entries in both games; on the host
`/Game/maps/ORWorld/Missions/Part1/obr/OBR1_Weather` was loaded and visible,
on the guest not loaded. Setting `bShouldBeLoaded`/`bShouldBeVisible` on the
guest's entry did nothing in 110 s. `GameplayStatics.PerformActionOnStreamingLevels`
(a Bend addition, not latent) loaded it: `streaming the partner's mission
weather .../OBR1_Weather (ok)`, then `loaded=1 visible=1`.

The weather level alone turned the guest's view nearly white: the guest kept
the host's synced `WorldWeather` 0 (clear) while the host's mission uses its
`TempDefaultWeather` (4). With the guest applying the mission's preset while
the level is streamed (`world sync: weather 0 -> 4`), the guest's screenshot
showed the same dark, misty overcast as the host's.

The level is released with `ClearActionOnStreamingLevels` when the host's
mission changes or ends; that release was not observed in this run.

## Region transition and the guest's mission state (2026-10-30, paired, one PC)

The host started `RNA1` (*Riding Nomad Again*, ID 70) out of order from the
lab's act-2 save.

1. Following the host's mission-start save put the guest at the mission's
   staging point (about (249658, 234496)), 4.2 km from the host's gameplay
   start. The post-load teleport fired while the host was still in its intro
   cinematic; it now waits until the host sends world state again (the host
   sends none during cinematics).
2. A later F7 teleport across 1.1 km dropped the guest far below the host
   (z -17243 against -1669): the ground there was not loaded. Long teleports
   (over 200 m) now hold the player at the destination until a capsule sweep
   finds ground, as `move-by` does: `holding 1143 m away until the ground
   there loads`, `placed on ground z=-1759 after 2.5 s`.
3. The start fade turned black after the post-load check; it is now watched
   once a second and lifted after 5 s (follower only: the local mission must
   be the one the partner runs).
4. 1.5 s after the teleport the game replaced the guest's pawn and respawned
   it at the staging point. Because the guest's game treats the host's
   mission as active (failure zones, staging point) without running it, the
   guest now defers story resync while the host runs a story mission
   (`story: waiting for the host to finish mission 'RNA1' before syncing`)
   and catches up at the host's first save after it.
5. With resync deferred, the guest stayed on its own act-2 save; the F7
   teleport into the Lost Lake encampment was still followed by a respawn at
   the guest's save position. That act-2 save has not unlocked Lost Lake, so
   this is most likely the game resetting a player in a locked region, an
   artefact of starting `RNA1` out of order. Region transitions need a test
   save that has reached them.

## Companion materials on the guest (2026-10-30, paired, one PC)

Follow-up to "Host's story companion on the guest". The host's Boozer
renders with runtime `MaterialInstanceDynamic`s; the new `DMMT` datagram
(own version 1, every 2 s per streamed companion mesh) carries, per slot, the
asset behind each instance (`Parent`, walked up to the first asset) and up to
two of its scalar overrides. `material-probe` lists each slot on both games.

- Host, `OBR`: every Boozer slot is a dynamic instance with `HC_ON=0`
  (the hands also have `wrinkleInfR1=1.88 wrinkleInfG1=1.88`), for example
  `boozer_up_thermal_burned_full` → `mi_boozer_thermal_burnt`.
  `FScalarParameterValue` is 0x20 bytes in this build; the first probe read
  it with a 28-byte stride and printed garbage entries (`<bad-name>`).
- Setting only the asset materials on the guest textured the head and
  pants, but the thermal shirt stayed invisible, so the head floated above
  the legs. With a dynamic instance per slot and the host's `HC_ON=0`, the
  guest's crops (`buddy-20261030-1736`, `g1.png`) showed the shirt.
- The hand materials have 99-character paths (`.../hand/mi_boozer_hand_burnt_not_bloody...`)
  and were dropped by the 96-byte slot limit; it is now 120 bytes (packet
  1,416 bytes).
- One run (`buddy-20261030-1744`) built the replica with only the head, and
  it was never rebuilt: the loaded meshes were apparently collected before the
  1 s completeness check, and assets were requested only once. Requests are
  now repeated after 10 s and the replica is rebuilt in the same tick the
  meshes are found. In `buddy-20261030-1752` the hand material was requested
  twice (10 s apart) before it stayed loaded. All four meshes were built with the host's
  materials (`companion replica materials applied (4 meshes)`). Three guest
  crops show Boozer fully textured: shirt, burnt hands, pants and boots.

Not tested: other companions, Boozer's bike, a two-PC run.

## Companion vehicles on the guest (2026-10-30, paired, one PC)

`vehicle-probe` (all live `VehiclePawn`s with their parts). In free roam
the only live vehicle is the player's own bike. In `OBR` the host gets
`BP_bike03Boozers_C` (`/Game/Vehicles/Bikes/BP_bike03Boozers.BP_bike03Boozers_C`):
Boozer drives and the host rides as a passenger, so the host's
`GetVehicle` (its "local bike") becomes Boozer's bike for about a minute.
Later the bike is parked 1 m from the host. It has 20 mesh components:
skeletal parts on sockets, parts that share the chassis skeleton, and static
wheels.

`spawn-actor` with that class on the guest (`spawn-20261030-2320`) built
the bike with the same 20 mesh components, fully textured. The guest's own
bike did not change. With physics off it hovered at the spawn height, and a
marker icon shows above it, as on the host's real bike.

`DMVH` (own version 1) streams every vehicle near the host except its own
bike; the guest spawns the class and interpolates the transform.
`vreplica-20261030-2332` and `-2338`:

- The host logged `vehicles: streaming BP_bike03Boozers_C_0` when Boozer's
  bike appeared, and the guest logged `vehicles: replica 1 is a
  BP_bike03Boozers_C` 0.1 s later.
- While the host rode on Boozer's bike, the guest did not clone its own
  bike there. The clone came back when the host dismounted, at the host's
  parked bike.
- One guest frame during the ride shows Boozer and the host on the bike,
  driving off across a bridge.
- Three guest frames after parking show Boozer's bike standing on the road
  next to the host, as in the host's frame.

Not tested: whether the guest can interact with the replica (ride prompt),
mission trucks, frame time with several vehicles, two PCs.

## Co-op review scenarios (2026-10-31, paired, one PC)

Two real games on the host PC, disposable `host`/`guest` reflink clones of
`run1` created by `pair.sh`, loopback UDP, `HostSave=1`, `FreeRoam=1` (base
INI). Both ran ASI `49b4311d…f9144`, the shared-pause build. Scripts, logs and
screenshots are archived outside the repo (`review-20261031-2337/`,
`review2-20261031-2350/`). This is **not** a separate-PC or Steam test.

| Scenario | Evidence | Result |
|---|---|---|
| Host time skip (`time-shift`, sleep analog) | Host `23:40:48.382` time 10.970 → 16.970; guest `23:40:49.000` world sync 10.971 → 16.972 | Pass |
| 16 debug enemies at the host | Host `23:40:56.967` streaming 16 local AI pawns; guest created 16 replicas; tick peaks 23–30 ms | Pass (debug cap is 16; no horde) |
| Host checkpoint reload (`reload-save`) | Host `23:42:33.287` `ReloadBecauseOfJobFailure invoked=1 returned=0`; no reload followed on either side | Inconclusive; host reload untested |
| Guest game killed (SIGKILL) | Host `23:43:55.035` marker hidden, stand-in removed; `sha256sum` of the guest's real `.sav` files identical before, after crash and at the end | Pass |
| Guest rejoins after the crash | Host `23:45:41.299` peer connected; guest adopted the host save, first load `ced003fb` ≠ `fa97ef49`, one retry, `23:47:06.115` verified `fa97ef49`, `23:47:12.956` moved to the host | Pass, with one reload retry |
| Host game killed | Guest `23:48:25.387` Host disconnected; local AI resumed (20 spawn areas, 7 crowd generators); own time/weather restored; Escape `23:48:45.370` `paused=1 managed=0` | Pass. The guest's saves stay in the sandbox until restart, with no in-game notice |
| Passenger | Run 2: `23:54:13.787` boarded on `SOCKET_PassengerSeat`; `23:54:25.244` dismounted, body, movement, collision and saving restored. Run 1's attempt pressed F9 6 s before the host's bike was streamed and was refused | Pass |
| Host 1.5 km jump (`move-by "1500 0"`) | `23:54:45.382` gave up, no ground at the destination | Not run; earlier 1.2 km free-roam tests remain the evidence |
| Guest teleport (F7) | `23:55:12.420` teleported; positions within ~1.5 m | Pass |

No `FATAL` lines in any log. The only errors were the `continue-title`
retries that `lab.sh enter` makes while the splash loads.

Game files: the host install and `run1` share `DaysGone.exe` SHA-256
`17c2fca0…37730b3`, Steam build 19221447 (Patch 1.13, 21 July 2025,
[SteamDB](https://steamdb.info/patchnotes/19221447/)), depot `1259421` only
(no DLC), one 30.7 GB `BendGame-WindowsNoEditor.pak` plus the branding pak.
The April 2025 *Remastered* update was build 18212596 (Patch 1.08); Steam's
`Classic` beta branch is Patch 1.07
([Bend](https://support.bendstudio.com/hc/en-us/articles/45621253674771-Steam-Beta-Branch-Option)).

## Native host load and fast travel (2026-11-01, paired, one PC)

Two real games on the host PC, disposable `host`/`guest` reflink clones of
`run1`, loopback UDP, `HostSave=1`, `FreeRoam=1`; both ran ASI
`49b4311d…f9144`. This was a one-PC lab run, not a separate-PC or Steam test.
Full logs are archived outside the repo (`travel-load-20261101/`).

| Scenario | Evidence | Result |
|---|---|---|
| Load an older host autosave from the native pause menu | At 09:07 the host loaded the Oct 24 autosave from the Load Game screen. Host world sync resumed at 09:08:02 (`time=10.377`); the guest restored local time `18.930 -> 10.386`. The host's position changed from `(-1445 -236761 440)` to `(-29960 -205984 -9908)`. The partner marker hid at 09:07:57 and returned at 09:08:07; the guest continued reporting the host, with no disconnect | Pass for session continuity, clock sync and marker rebuild; death and changed-story reloads not tested |
| Host fast travel to O'Leary Mountain Safehouse | At 09:10:58 the host synced at `time=12.356`, rebuilt its avatar, and hid/showed the partner marker (09:10:58–09:11:03). The guest synced `11.550 -> 12.356` at 09:10:58. The host arrived at `(2002 -239652 421)` while the guest remained at `(-30177 -205938 -9854)`, about 466 m horizontally away | Pass for host travel and time sync; guest did not teleport |
| Safehouse sleep | The host arrived at the camp and was navigated around its structures, but the bedroll interaction was not reached or activated | Not tested here; see "Death, sleep and travel follow-up" |
| Host death | No death was triggered in this run | Not tested here; see "Death, sleep and travel follow-up" |

After fast travel, the guest continued receiving host positions as the host
moved around the camp. The travel itself showed normal update timing afterward
(about 1.1–1.2 ms mean, 22.6–23.2 ms peak in the sampled intervals); this
does not validate streaming ownership handoff at the resulting separation.
The host did not carry the guest to the destination, so regrouping remains a
player-facing gap. The base isolated game executable and `.sav` hashes matched
before and after the run; both disposable clones were stopped and destroyed.

## Death, sleep and travel follow-up (2026-11-01, paired, one PC)

Two real games on the host PC used disposable clones of `run1`, loopback UDP,
`HostSave=1`, `FreeRoam=1` and ASI SHA-256
`49b4311def3b59db7e04ac0d6e869a4695252603b9317248f9989d970b3f9144`. Logs,
screenshots and setup scripts are archived outside the repo
(`death-sleep-20261101-t1/`, `death-confirm-20261101/`). These are not two-PC
or Steam-transport tests.

| Scenario | Evidence | Result |
|---|---|---|
| Host death | In the confirmation run, 16 debug-spawned swarmers attacked. Host health was `121.875` at 12:40:19.704 and `0.000` at 12:40:20.220; the debug session automatically cleaned up and restored saving. The host pawn disappeared at 12:40:21.659, a new pawn appeared at 12:40:26.732 and health was `121.875` at 12:40:39.332. On the guest, the marker hid at 12:40:21.688 and returned at 12:40:27.074; later host movement was received | Pass for this debug-enemy death/respawn and session continuity |
| Death clock | While the host pawn was absent, the guest restored its local time `11.630 -> 10.386` at 12:40:22.005; it recaptured local time/weather as host sync resumed at 12:40:27.073. An earlier low-health reload also briefly restored local time before resync | Visible time discontinuities are possible during host reload |
| Host and guest fast travel | At 11:41:37 host travel split the players at 446 m; host `stream-probe` reported the guest focus active, and the guest resumed local AI generators. Guest fast travel reunited them at 45 m at 12:16:33; the host focus became inactive. Guest time was corrected `9.228 -> 8.451` to the host at 12:16:32 | Split/merge transitions observed; no guest-owned enemies were retained or fought |
| Host sleep | The host used the O'Leary bed while the guest remained at its original location. Host time advanced `1.982 -> 7.013` between 12:12:52 and 12:12:56; guest corrections reached `6.496 -> 7.013` at 12:12:57. Both continued reporting the partner afterward | Pass for host sleep and guest clock in this separated run |
| Guest sleep | Two connected attempts showed the bed prompt and interaction progress, but no sleep animation or time advance. After the host stopped, the same guest used the bed: screenshots show the sleep animation and the map clock changing from 10:30 AM to 6:10 PM | Connected behavior needs investigation; time and exact position differed in the disconnected comparison |
| Indoor teleport | Guest F7 at 12:17:17, with the host beside the bed, placed the guest among ceiling supports; the host screenshot showed the guest's feet through the ceiling. Moving the host into open floor and retrying F7 recovered the guest | Unsafe placement observed once |
| Tutorial pause | With the host's native “Fight or Flight” tutorial displayed, `time-probe` at 12:39:18 reported host `paused=1`, `native-menu=0`, `local=0`, `peer=0`, while the guest reported `paused=0`. After dismissing the tutorial, the host reported `paused=0` | A tutorial can pause one side outside the shared-menu vote |

Both guest save guards reported active. In each run, both copies of the 11
guest real `.sav` files matched the isolated base's originals, including after
the guest's disconnected sleep. Restored base ASI, INI and `Saved` tree;
executable and file manifests verified; all four clones destroyed. Not tested:
guest death, natural guest-owned enemy combat, mission-time sleep/death, and
separate-PC behavior.

## Join block, no-INI v12 join and native PLAYER SETTINGS (2026-11-06, one PC paired, UDP)

- **Build:** mod commit `86564ad` (protocol v12; ASI SHA-256 `970f85d7…`), game build 19221447.
- **Setup:** two reflink lab clones on the host PC, guest under the sandbox
  Steam account. Each clone's old `TheysGone.ini` was deleted; the lab wrote
  only `Transport=udp`, `PeerHost`, `Port`, `PeerPort`, `PlayerName` and
  `DebugTools=1`. Scenario B added `DebugFailSaveGuard=1` on the guest.
- **Steps:** B: host into gameplay, guest started and auto-joined. A: same
  without the switch, then 150 s of play. In A the host then opened pause →
  CO-OP → PLAYER SETTINGS, cycled JACKET, pressed FRIENDLY FIRE three times,
  went BACK and chose TELEPORT TO PARTNER, then closed the pause menu.
- **Result:** PASS for every check below.
- **Observed:**
  - B: 3 ms after `peer Host … connected` the guest logged `save guard:
    sandbox unavailable; leaving the session so your real saves are not
    written`, then `session event: Join blocked: save sandbox failed`. It never
    requested the host's save, so its title menu had no Continue. The host
    logged `Guest disconnected`. The HUD notice failed at the title screen
    (`native ui: cannot display session notification; HUD unavailable`).
  - A: both logged `starting (protocol v12)` and `objective sync: enabled when
    hosting` with no such INI key. The guest enabled the sandbox right after
    the host's first packet, adopted snapshot `57b07cba` (11 files, 7,713,260
    bytes), reloaded and logged `verified loaded host progress fa97ef49`;
    story signatures then matched. All guest writes went to the sandbox.
  - PLAYER SETTINGS rendered in the pause menu with values in the labels.
    JACKET cycled to CRIMSON: the host's jacket turned red, the INI gained
    `JacketColor=1`, and the guest logged `appearance: deacon_up_standard
    palette=Crimson` for the partner at the same second. FRIENDLY FIRE went
    OFF → PRESS AGAIN TO ENABLE → ON - LETHAL (status: `Partner has not
    enabled friendly fire`) → OFF. TELEPORT TO PARTNER closed the CO-OP page
    and logged `teleported to partner Guest` after the pause menu closed.
- **Derived:** the join block and host-decided options work on one PC over
  UDP, and a player needs no INI beyond the LAN pairing keys. The native page
  replaces the F8 overlay for these rows. At the title screen the block is
  visible only in the CO-OP lobby log and status, not as a HUD notice.
- **Unknown:** the title-menu PLAYER SETTINGS page, the "sandbox stays active"
  notice on leaving, F9 / View+X, the guest-side page, gamepad navigation, the
  Steam transport and two-PC behavior.
- **Cleanup:** both clones destroyed; base saves hashed before, after and at
  the end, unchanged. Logs and screenshots archived outside the repo
  (`<archive>/v12-native-20261106T180216/`).

## Title-menu settings, F9 boarding and the leave notice (2026-11-06, one PC paired, UDP)

- **Build:** run 1 used the ASI from the previous entry (`970f85d7…`); run 2
  used the fixes below (ASI SHA-256 `2b99580e…`), game build 19221447.
- **Setup:** as in the previous entry: fresh reflink clones, no INI beyond the
  LAN pairing keys and `DebugTools=1`, guest under the sandbox Steam account.
- **Steps:** host at the title screen: CO-OP → PLAYER SETTINGS, select
  TROUSERS twice. Both into gameplay; host holds E to mount the bike, guest
  holds F9. Guest: pause → CO-OP, open PLAYER SETTINGS, BACK, then LEAVE
  SESSION. Run 2 repeated the title-menu and leave steps.
- **Result:** run 1 PASS for the title page, F9 and the guest page; two bugs
  found. Run 2 PASS for both fixes.
- **Observed:**
  - The title CO-OP lobby page listed START GAME | CLOSE SESSION | PLAYER
    SETTINGS | BACK. PLAYER SETTINGS had no FRIENDLY FIRE row outside
    gameplay. TROUSERS went ORIGINAL → CRIMSON → OCEAN and the INI gained
    `TrousersColor=1`, then `2`; the guest logged
    `deacon_lo_standard_jeans palette=Ocean` for the partner on joining.
  - Bug 1 (run 1): after each change the title highlight was drawn on JACKET
    while the cursor stayed on TROUSERS (the next press changed TROUSERS
    again). The reflection dump shows the title widget's
    `SetCursorPosition(Position, CallFunc_NotEqual_IntInt_ReturnValue)`, so
    restoring the same position after the label refresh does not redraw. The
    pause menu was not affected. Fix: step the cursor off the row and back.
    Run 2: the highlight stayed on `TROUSERS: CRIMSON`.
  - F9: `passenger: boarded using socket SOCKET_PassengerSeat`; the guest
    screenshot shows it seated behind the host.
  - The guest's pause CO-OP page listed LEAVE SESSION | TELEPORT TO PARTNER |
    PLAYER SETTINGS | BACK; its PLAYER SETTINGS page had `FRIENDLY FIRE: OFF`.
  - Bug 2 (run 1): LEAVE SESSION logged `save guard: left the session; the
    sandbox stays active until restart`, but the HUD notice was raised while
    the pause menu was open and had expired when it closed; nothing was
    visible. Fix: send it as a session event. Run 2: `session event: Saves stay
    in the co-op sandbox until you restart` appeared in the CO-OP status log
    right after leaving.
- **Derived:** the native settings page works in both menus. Session events
  raised while a menu is open are shown only in that menu's log, not later on
  the HUD.
- **Unknown:** gamepad navigation (the lab sends keyboard input only), the
  Steam transport and two-PC behavior.
- **Cleanup:** clones destroyed; base saves hashed before and after both runs,
  unchanged. Archives: `<archive>/v12-ui2-20261106T193522/` and
  `<archive>/v12-ui2b-20261106T194744/`.

## Two-PC Steam session without an INI (2026-11-06, two PCs, Steam)

- **Build:** run 1 ASI `2b99580e…`, run 2 `ff0d662a…` (the teleport and
  wording fixes below), protocol v12, game build 19221447 on both.
- **Setup:** host on the host PC (desktop Steam account), guest on the Steam
  Deck (its own account), real installs. Before the run, each PC's ASI, INI
  and whole Proton `Saved` tree were backed up with hashes; both INIs were
  moved aside so the run used built-in defaults (`Transport=menu`). The Deck
  had no branding pak. Run 1 was played by the user (Deck with its
  gamepad); run 2 was driven remotely with a temporary uinput keyboard and
  `gamescopectl` / `spectacle` screenshots.
- **Result:** joining, save transfer, settings, teleport (after the fix) and
  riding PASS; enemies and tutorial pause FAIL; riding has defects.
- **Observed:**
  - Host CO-OP → HOST ONLINE GAME opened a FRIENDS ONLY lobby; the Deck's JOIN
    A FRIEND listed the host and joined it (`Joined <host>'s lobby`).
  - The guest enabled its sandbox and received snapshot `10a18929` (10 files,
    7,547,924 bytes) in 36 s. The host logged 1,313 `steam: send failed
    (result 25)` lines; the guest pulls 24 × 1 KB chunks per tick, more than
    Steam's send rate. Run 2: 1,380 such lines.
  - The guest's CO-OP lobby said `Host save ready: press Continue` while its
    row was START GAME (JOIN HOST). The guest could load the host's save and
    enter the world while the host was still at its title menu.
  - In gameplay: both logged `status: partner … visible=1`, about 115–120 pose
    packets/s each way, and story `fa97ef49` on both. The Deck's mod tick ran
    at 37–41 updates/s (mean about 5 ms, peaks 180–195 ms) against 60/s on
    the host.
  - With the gamepad: PLAYER SETTINGS cycled JACKET to FOREST and SAND. TELEPORT
    TO PARTNER closed only the CO-OP page; the teleport ran when the user
    then closed the pause menu.
  - Riding: the native prompt read **Ride with <host>** with descenders cut
    off; boarding logged `boarded using socket SOCKET_PassengerSeat`. While
    seated the passenger could still attack, the held weapon looked
    misplaced, the logged **Get off** label never showed, and there was no
    mount animation.
  - The "Fight or flight" tutorial paused the host's game only.
  - A swarm attacked on the host. The host never logged `streaming N local AI
    pawns` in the whole session, so no enemy reached the guest, which had
    despawned its own AI (`the host owns enemies`). Host:
    `partner stand-in took 40.0; forwarded to the owner`; guest: `partner
    enemy hit you for 40.0`.
  - Run 2: `save transfer: host save installed; START GAME or Continue loads
    it`; TELEPORT TO PARTNER logged `pause CO-OP closed` and `teleported to
    partner <host>` 19 ms apart, and the Deck screenshot showed gameplay
    with no menu open.
- **Derived:** the no-INI Steam path from the native menus works on two PCs.
  Enemy sync is broken for natural enemies, which makes fights unplayable for
  the guest. `SwarmerPawn_C` derives from `BendAIPawn` through
  `BendFreakerPawn` and its controller from `BendFreakerController`, so the
  class filter should accept swarmers; the cause is unknown (suspect: horde
  swarmers driven by `BendCrowdGroupHorde` without their own controller,
  skipped as pooled). The reflection dump shows a native seat API
  (`TryEnterVehicle`, `ReserveSeat`, `AddRider`) and a passenger camera
  class (`BendVehiclePassengerCamFollow`).
- **Unknown:** why the enemy scan selected nothing; the cause of the Deck tick
  peaks; whether a virtual gamepad reaches the game on the Deck (the uinput
  keyboard did).
- **Cleanup:** both games quit through the menu. ASI, INI and `Saved` were
  restored on both PCs and their hashes matched (85 files on the host PC, 57
  on the Deck). Logs and the post-test `Saved` trees are kept in each PC's
  `~/TheysGone-backup-20261106-twopc/`.

## Anim mirror: can anim-instance variables replace bone streaming? (2026-11-06, one PC, one game)

- **Question:** Source-style games send animation inputs, not bones. Can a
  pawnless clone that runs the same AnimBP reproduce a freaker's pose from the
  source anim instance's reflected variables alone?
- **Build:** worktree `anim-mirror-probe`, final ASI SHA-256 `10d0d6d3…`,
  game build 19221447. Commands `anim-mirror-bare`, `anim-mirror-on` and
  `anim-mirror-off` ([debug tools](DEVELOPMENT.md#debug-tools)).
- **Setup:** one headless lab clone (`mirror`), `DebugTools=1`, no partner.
  `begin`, `god-on`, `spawn-single`; then 24 s bare control, `probe-enemy`,
  off; 24 s copy mode, `probe-enemy`, off; `end`. Four runs; the first three
  found tooling and probe bugs.
- **Result:** FAIL for the hypothesis. Copying the variables does not make the
  clone match: mean bone error median 59.0° (p90 136.9°) with copying against
  50.9° (p90 66.7°) for the bare control.
- **Observed:**
  - The clone `SkeletalMeshActor` got `SwarmerBVim_C` on `swarmer_horde_c`
    (136 bones) with `BendPawn=null`. Its native anim update did not crash
    and did not overwrite copied values: the clone's `Speed` equalled the
    source's on every copy-mode sample (0, 3, 85, 150, 500).
  - The copyable set (plain values, structs up to 48 bytes and anim-asset
    references declared from `BendCharacterAnimInstance` down, including the
    Blueprint class) is 273 fields, **1,267 bytes per frame** before any
    compression.
  - Run 2: `GetCurrentStateName(MachineIndex=0)` crashed the game.
    `MachineIndex` is an anim-node property index, not a state machine
    number; the probe no longer reads state names.
  - Run 3: without pinned LOD, both modes alternated between about 15–20° and
    about 90° mean error (medians 91.5° bare, 87.7° copying). Run 4 forced
    LOD 0, `MeshComponentUpdateFlag=0` and no update-rate optimization on both
    meshes; `PredictedLODLevel` then read 0/0 and the bare control was steady
    at 37–68°.
  - Run 4 copy mode had stretches of 122–144° mean error while the source
    stood at `Speed=3`, and 51–73° at `Speed=85`; it never stayed below the
    bare control.
- **Derived:** the variables reach the clone, but the pose depends on state
  they do not carry: which animation each state machine and asset player
  picked (random-sequence groups included), playback times, montages and
  probably native reads from the pawn. Copying event flags (attack, grapple,
  one-shot) without that state appears to drive the clone into wrong poses.
  The full variable set (1.3 KB) is also close to the streamed bones'
  ~1.7 KB, so this form saves no bandwidth either.
- **Unknown:** the error floor of the metric (no identical-pose reference was
  measured); whether syncing asset-player times, state-machine states or
  montages on top would close the gap; other enemy types; two-PC behavior.
- **Cleanup:** lab clone destroyed after each run; the base game copy is never
  written. `~/TheysGone-lab-tools/debug_control.py` was replaced by the
  worktree version (backup `debug_control.py.bak-before-anim-mirror`).
  Archives: `<archive>/anim-mirror-20261106-2112/` through
  `<archive>/anim-mirror-20261106-2128/`.

## Pawn mirror: native-style enemy animation from movement and state (2026-11-06, one PC, one game)

- **Question:** native Unreal clients animate another machine's enemy from its
  movement plus state events, on a real pawn. Can a guest-side swarmer pawn
  without AI animate correctly from the host's inputs alone, and which inputs
  are needed?
- **Build:** worktree `anim-mirror-probe`, final ASI SHA-256 `af1276ed…`, game
  build 19221447. New modes `anim-mirror-nodes` and `anim-mirror-pawn
  [--arg <options>]` ([debug tools](DEVELOPMENT.md#debug-tools)).
- **Setup:** as in the previous entry. From run 9 on, a chase pattern moved the
  player 10 m every 3 s (`move-by`) so the swarmer kept walking, turning and
  stopping instead of grappling the god-mode player.
- **Metrics:** bone error as before, plus a phase-insensitive **action match**:
  whether the highest-weighted body animation (facial `_face_` layers
  excluded) is the same asset on source and clone.
- **Result:** PARTIAL PASS. Locomotion works natively once the AI's movement
  request is sent; attacks and grapples do not yet.

| Run | Mode | Bone error median | Action match | Clone played |
|---|---|---|---|---|
| 5 | `nodes` (graph state copied) | crashed after ~10 s | – | – |
| 6 | `pawn` | 67.4° | – | idle |
| 7 | `pawn` + `move-tick`/`keep-controller` variants | 55.5–64.5° | – | idle |
| 9 | `pawn`, `pawn:intent`, `pawn:copy-vars` | 52.0–57.9° | 0–3 of 55–57 | idle |
| 10 | `pawn:state`, `pawn:intent,state` | 64.4–67.6° | 0–1 of 57 | idle |
| 12 | `pawn:intent,state` + movement request | 44.7° | 14 of 57 | movement, turns, walks |
| 13 | `pawn:move-request` (no anim variables copied) | 53.3° | **26 of 57** | movement, turns |
| 13 | `pawn:move-request,state` | 58.4° | 17 of 58 | movement, turns |
| 13 | `pawn:intent,state` (run 12 repeated) | 61.9° | 10 of 54 | movement; grapples missed |

- **Observed:**
  - Run 5: the graph has 12 state machines and 123 asset players; median
    3–5 players are active at once (max 11). Writing state-machine
    `CurrentState`/`ElapsedTime` raw crashed the game.
  - A pawn of the source's class (`SpawnAIFromClass`, look from the source,
    brain stopped, controller unpossessed and destroyed, collision off) gets
    its own `SwarmerBVim_C` with `BendPawn` set and a live
    `StateMachine_Swarmer_C`. Its anim `Velocity` follows the copied movement
    `Velocity` (for example 126 → 147, 58 → 59).
  - Anim `Speed` is not velocity: the source shows tiers (0, 3, 85, 150, 450,
    500) while its velocity is around 80. Run 9's divergence log (pawn fed
    velocity only) listed what the clone does not derive: `Speed`,
    `DesiredSpeedNotSlowedDownForTurn`, `DesiredVelocity*`,
    `DeltaToDesired*`, `DesiredTurnAngleAsFloat`,
    `WantsToTransitionIdleToMove`/`MoveToIdleNEW` and others (64 variables).
  - `BendPawn.AnimStateMachine` (`StateMachine_Swarmer_C : BendAnimStateMachine`)
    is Bend's gameplay anim state. `IsInStateEnum` and
    `RequestStateChange(State)` work on the clone: GroundMovement (0),
    MeleeAttack (5) and Grapple (6) were forwarded, 23 of 23 accepted in
    run 10. Forwarding alone left the graph idle.
  - Run 11 moved the clone without `bTeleport` and copied
    `bHasTouchedGroundAfterSpawningOrTeleporting`; still idle.
  - Run 12 also copied the movement component's `Acceleration`,
    `RequestedVelocity`, `bHasRequestedVelocity`, `desiredSpeed`,
    `DesiredRotation`, `DeltaToDesiredRotation` and `MaxWalkSpeed`. The clone
    then played `Swarmer_Movement`, `Swarmer_Idle_to_Move`,
    `BlendSpace2D_Swarmer_TurnInPlace` and `swm_h_walk_fwd_*`. 14 samples
    matched exactly; most others were the same kind of movement at another
    moment (`walk_fwd_a` vs `walk_fwd_d`, `TurnToMove` vs `TurnInPlace`,
    `Movement` vs `Idle_to_Move`).
  - Attacks (`swm_atk_low_run_5M`) and grapples were not reproduced; the clone
    played `Idle_to_Move` instead.
  - The `pawn:ai` control (clone keeps its AI, not fed) idled for all 57
    samples, so it did not establish what a working clone scores.
  - Run 13 (bisect): with the movement request but **no anim variables
    copied**, `Swarmer_Movement` matched in 23 samples and the clone played
    only locomotion assets. Its divergence log still listed 48 variables
    (`SmoothSpeedNotSlowedDownForTurn`, `DesiredVelocityInterpolated_*`,
    `DeltaToDesiredMovementDirectionYaw` and others), so the clone derives
    those differently without changing the action. Attacks (`swm_atk_*`)
    showed as `Swarmer_Idle_to_Move` on the clone in all variants.
  - Bone error did not follow the action match (53–62° in run 13).
- **Derived:** the swarmer's locomotion graph is driven by the AI's movement
  request (acceleration, requested velocity, desired speed and rotation), not
  by the resulting velocity. Native inputs alone (position, velocity,
  movement mode and that request, no anim-instance variables) are enough for
  locomotion on a pawn without AI: about 60 bytes before quantization,
  against about 1.7 KB for streamed bones. Forwarding the Bend anim state did
  not improve locomotion; each variant saw a different stretch of the fight,
  so the differences between run 13's variants are not significant. Attack
  and grapple animations need their own event data (which attack, its target
  and timing). Bone error is not a usable pass/fail measure for this approach.
- **Unknown:** the attack and grapple event data; hit reactions and death;
  the behavior of other enemy types; what a correct clone scores (no working
  control yet); two-PC behavior and the real wire format.
- **Cleanup:** lab clone destroyed after each run; base game copy never
  written. Archives: `<archive>/anim-mirror-20261106-2147/` through
  `<archive>/anim-mirror-20261106-2226/`.

## Pawn mirror: attacks and grapples as replayed slot montages (2026-11-06, one PC, one game)

- **Question:** the pawn clone walks natively but never attacks or grapples.
  What one-off event makes a pawn without AI play them?
- **Build:** worktree `anim-mirror-probe`, game build 19221447. New
  `anim-mirror-pawn` options `attack-data`, `start-attack` and
  `play-oneshot`; the clone's weapons get collision turned off.
- **Setup:** as before, plus a stand-still scenario for attacks: per mode, a
  fresh `spawn-single`, a `probe-enemy` hit to provoke it, 40 s with the
  player standing still. God mode stayed on throughout.
- **Result:** PASS in the lab for attacks and grapples as replayed
  montages; damage from the clone not yet measured.
- **Observed:**
  - A ProcessEvent trace of a fight showed no `StartMeleeAttack`,
    `AttemptDirectGrapple` or `ForceDirectGrapple` calls: the AI calls them
    natively. Visible were `AnimState_AIMeleeAttack_C.OnEnterState` per attack
    and anim notifies naming the attack assets.
  - `attack-data`: copying the source anim's `BendAttackData` (`AttackAnim`
    and the rest, minus its TArray) before forwarding MeleeAttack was
    accepted but the clone's graph played no attack. In one run the clone was
    locked after a forwarded `PerformAction` (4): every later request was
    refused. PerformAction is no longer forwarded.
  - `start-attack`: `StartMeleeAttack(target, name, IgnoreDistance)` on the
    clone returned no attack for every one of the ~25 `BendBTTask_Attack`
    names in loaded behavior trees (`Unarmed`, `Pursuit`, `Leap`, `Swipe`,
    `RunAttack` and others).
  - The graph has slots `UpperBody`, `FullBody` and `face`.
    `play-oneshot`: while the source is in MeleeAttack or Grapple, each new
    body `AnimSequence` it plays is played on the clone with
    `PlaySlotAnimationAsDynamicMontage(asset, FullBody)`. Every call returned
    a montage. The action match now counts the montage's asset while
    `Montage_IsPlaying`.
  - Final run, two modes of 41 s: **23 of 26** and **51 of 60** one-shot
    samples matched; overall action match 28 of 81 and 57 of 82.
  - Bone error confirms it independently: during matched grapple montages it
    fell to **3.2–20.3°** (`swm_b_grapple_01_loop_105frames_swm` 3.2–8.6°,
    `killdcn` 7.2–20.3°), against 30–70° medians elsewhere.
  - The looping `grapple_01_loop_105frames` montage was played once; when it
    ended the clone fell back to idle (59–73°) while the source kept looping.
  - Matched attacks such as `swm_atk_low_run_5M` kept a high bone error
    (59–84°).
  - The clone's weapons (3 per swarmer) had their collision turned off at
    spawn.
- **Derived:** native-style sync for a swarmer is movement plus the AI's
  movement request every update, and a one-off event per attack or grapple
  phase (the animation asset, played as a slot montage). That matches how
  native Unreal replicates one-off animations. Bone error is a usable check
  only when both sides play the same animation; its floor is about 3–20°.
  The high error on matched attacks is probably root motion or anchoring to
  the target, which the clone lacks.
- **Unknown:** whether the stand-in's attacks or claws damage the player
  (god mode was on); looping one-shots; hit reactions and death; anchoring
  and root motion of attacks; other enemy types; two-PC timing; the wire
  format.
- **Cleanup:** lab clone destroyed after each run; base game copy never
  written. Archives: `<archive>/attack-trace-20261106-*/` and
  `<archive>/anim-mirror-20261106-2244/` through
  `<archive>/anim-mirror-20261106-2316/`.

## Pawn mirror: stand-in damage with god mode off (2026-11-07, one PC, one game)

- **Question:** does the guest-side stand-in, replaying attacks as slot
  montages, damage the player? Does turning off its weapons' collision stop
  that?
- **Build:** worktree `anim-mirror-probe`, game build 19221447. The mirror now
  logs every `ReceiveAnyDamage` on the player with its `DamageCauser` and
  instigator, attributed to the source, the stand-in or neither through the
  owner, instigator and controller chain, and counts both meshes' anim
  notifies. New `anim-mirror-pawn` options: `heal` (hold the player's health
  at 1000), `keep-claws` (leave the stand-in's weapon collision on) and
  `no-offset` (put the stand-in on the source instead of 3 m beside it).
- **Setup:** the stand-still scenario with `god-off`: per run, a fresh
  `spawn-single`, `anim-mirror-pawn` with `move-request,state,play-oneshot,heal`
  plus the options below, two `probe-enemy` provocations and 40 s.
- **Result:** PASS in the lab for the claw fix: with collision off, the
  stand-in swung and dealt no damage; with it on and overlapping the player,
  it did.
- **Observed:**

  | Stand-in | Weapon collision | Runs | Stand-in `MeleeArmed` | Stand-in hits | Source hits |
  |---|---|---|---|---|---|
  | 3 m beside the source | off | 1 | 5 | 0 | 5 (355) |
  | 3 m beside the source | on | 2 | 13, 2 | 0, 0 | 11 (542), 3 (250) |
  | on the source | on | 2 | 11, 13 | **6 (301), 6 (293)** | 8 (406), 10 (476) |
  | on the source | off | 2 | 5, 5 | **0, 0** | 6 (398), 5 (355) |

  - Every stand-in hit named one of the stand-in's own `Bend_WpnSwarmerClaw_C`
    weapons as the causer, with no instigator (the stand-in has no
    controller). Source hits named the source's claws and
    `SwarmerController_C`.
  - Holding health does not prevent death: an uncountered grapple ends in
    `swm_f_grapple_1_killdcn_v2_swm` and a 145 hit with the source pawn as the
    causer, and the player died in most runs. Every damage event logged the
    player's health as 135.0, although the probe held it at 1000.
  - The replayed montages fire the animations' gameplay notifies on the
    stand-in: `MeleeArmed`, `CanCounterMyAttack`, `CanCounterMyAttack_State`,
    `CounterIcon`, `AttemptGrapple`, `SetReactionState` and `BloodStainFX`.
    `CanBeThrown`, `SetMovementMode` and `SuppressAnchorDriven` fired on the
    source only.
  - One-shot matching held at 19 of 21 to 48 of 48 samples per run.
- **Derived:** the stand-in's claws are real weapons that hit whatever they
  touch. Three metres to the side they missed. Turning off their collision at
  spawn stopped every hit in the overlapping runs, so it is required. The
  notifies are a second channel: they run gameplay logic on the guest (a
  counter prompt, a grapple attempt), not just visuals.
- **Unknown:** what the stand-in's `AttemptGrapple`, `CounterIcon` and
  counter-window notifies do on a real guest (no grapple of the player by the
  stand-in was seen, but the source always grappled first); whether
  `BloodStainFX` and other cosmetic notifies should stay; why the logged health
  read 135.0; other enemy types; two-PC timing.
- **Cleanup:** lab clone destroyed after each run; base game copy never
  written. Archives: `<archive>/anim-mirror-20261107-0926/` through
  `<archive>/anim-mirror-20261107-0955/`.

## Pawn mirror: what the stand-in's anim notifies do, and muting them (2026-11-07, one PC, one game)

- **Question:** the replayed montages fire gameplay notifies on the stand-in
  (previous entry). What does each one do on the guest, and can the
  stand-in mute them without losing the animation?
- **Build:** worktree `anim-mirror-probe`, game build 19221447. The mirror
  records the Blueprint events called while each source or stand-in notify
  runs (nested ProcessEvent calls; Blueprint calls into native functions do
  not pass through ProcessEvent and stay invisible). It also counts
  `Received_NotifyTick` and skeleton `AnimNotify_*` events. New options:
  `mute-notifies` swallows all of the stand-in's notify events;
  `mute-gameplay` swallows only `MeleeArmed`, `CounterIcon`,
  `CanCounterMyAttack`, `CanCounterMyAttack_State`,
  `CanProvokeCounterMyAttack_State`, `AttemptGrapple` and `DamageModifier`.
- **Setup:** stand-still scenario, `god-off`, stand-in on the source
  (`no-offset`), options `move-request,state,play-oneshot,heal`. Trace run with
  the claw fix; one `mute-notifies` run; three `mute-gameplay` runs with
  `keep-claws` (weapon collision on).
- **Result:** PASS in the lab for `mute-gameplay`: with claw collision on and
  the stand-in overlapping the player, it dealt no damage in three runs, and
  the replay still matched.
- **Observed:**
  - Traced calls: the stand-in's `MeleeArmed` calls its own claws'
    `OnWeaponArmed` and `OnWeaponDisarmed`, like the source's. Its
    `OnGroundIdle`/`OnGroundMove` notifies call only its own
    `BendClimbingSurfaceCharacterMovementComponent`. `CounterIcon`,
    `CanCounterMyAttack*` and `AttemptGrapple` made no visible Blueprint
    calls on either pawn.
  - The source's `GrappleStarted` (a state-machine notify the stand-in does
    not fire) calls `OnHCMSettingChanged` on hundreds of world actors, such as
    pickups, spawn locations and the bike.
  - Reflection of the compiled notify Blueprints (their local call results)
    shows what the invisible ones do. `AttemptGrapple`: `GetController`, cast
    to `Bend_AIController`, `GetAttackManager`, `GetFireFocusConst`, then
    `AttemptDirectGrapple` or `ForceDirectGrapple`. `CounterIcon`:
    `GetOwner`, `CounterIcon_01`, `CounterIconVariable`, so a counter-icon
    widget. `BloodStainFX`: `GetSocketLocation` only. `CanCounterMyAttack`:
    `GetOwner` cast to `Bend_Pawn`. `MeleeArmed` also carries the hit
    settings (`Damage Percent`, `Killing Blow`, `DisableVictimsDamageReaction`).
  - `mute-notifies`: 615 stand-in notify events swallowed in 16 s; 21 of 22
    one-shot samples still matched; no stand-in damage.
  - `mute-gameplay` with `keep-claws`: stand-in hits 0, 0 and 0 against 9,
    3 and 1 source hits; one-shot samples matched 43 of 43, 22 of 27 and
    17 of 17. Without muting, the same setup gave 6 stand-in hits per run.
  - `counter-for-target` (with `mute-gameplay` and `keep-claws`) latches the
    source AI's `GetCurrentTargetPawn` per replayed one-shot, as an attack
    event would carry it, and passes the counter notifies only when that is
    the local player. Attacks on the player: 166 counter events passed, 0
    muted. With `pretend-other-target`: 0 passed, 120 muted. Stand-in hits 0
    in both, against 6 and 3 source hits; one-shot samples matched 36 of 38
    and 25 of 28.
  - The game exited once, at `spawn-single` after a completed
    `mute-gameplay` run with the mirror already stopped. It left no crash dump
    and no log line, and it did not recur in the next two cleanup and spawn
    steps.
- **Derived:** muting `MeleeArmed` keeps the stand-in's claws disarmed, so
  it is a second guard next to weapon collision. `AttemptGrapple` needs an AI
  controller and cannot grapple on a controllerless stand-in. The counter
  notifies are what should depend on the target: on a guest, a counter
  prompt is right for an enemy attacking the guest and wrong for one
  attacking the host. A replicated attack event therefore carries the
  target, and the stand-in gates the counter notifies on it; the gate
  switches correctly in the lab. A guest who counters a stand-in still has to
  reach the host's real enemy, which is not designed yet. Cosmetic notifies (blood, face) and the stand-in's own movement
  notifies can stay.
- **Unknown:** whether the stand-in's `CounterIcon` actually draws a prompt
  on a guest screen (on one PC the source draws the same prompt); what the
  native parts of the counter notifies change; the one unexplained exit;
  other enemy types; two-PC timing.
- **Cleanup:** lab clone destroyed after each run; base game copy never
  written. Archives: `<archive>/anim-mirror-20261107-1009/` through
  `<archive>/anim-mirror-20261107-1028/`.

## Counter test: scripted counters against the source and the stand-in (2026-11-07, one PC, one game)

- **Question:** what does a successful counter do to the attacking enemy,
  and where does a counter against the stand-in land? Both are needed to
  design a "guest countered attack N" message.
- **Build:** worktree `anim-mirror-probe`, game build 19221447. New
  `anim-mirror-pawn` options: `auto-counter` calls the player controller's
  `TryToCounterAttack` 0.15 s after a `CanCounterMyAttack_State` window
  opens; `counter-key` sends the `F` key with `SendInput` instead;
  `mute-source-counter` mutes the source's counter notifies, so only the
  stand-in's window is open; `dump-input` logs the action mappings. Every
  ProcessEvent named `*Counter*`, `*Melee*` or `*Attack*` on the player, its
  controller and anim instance, the source and the stand-in is logged.
- **Setup:** stand-still scenario, `god-off`, options
  `move-request,state,play-oneshot,heal,mute-gameplay` plus the counter
  options; stand-in-only runs also with `no-offset,counter-for-target`.
- **Result:** FAIL. No counter succeeded, so the question is still open.
- **Observed:**
  - Input mappings: `Parry` (the counter) is bound only to
    `Gamepad_FaceButton_Top`, both in `InputSettings` and in the live
    `BendInputMapper`; it has no keyboard key. `Melee=F`,
    `Player_Roll=SpaceBar`.
  - `TryToCounterAttack`: 8 calls inside open windows (2 in the source's, 6
    in the stand-in's). All returned with `IsAttemptingCounter=0`, and no
    counter event followed.
  - `counter-key`: 26 `F` presses inside the source's windows. No counter
    event, and no `*Melee*` event on the player, its controller or anim
    instance either. Whether `SendInput` reaches the game under Proton and
    gamescope is not established.
  - The source anim's `AnimNotify_MeleeAttack_Start/End/ExitState` events are
    visible in ProcessEvent.
- **Derived:** `TryToCounterAttack` alone is not the button path; its
  preconditions are unknown. The counter is a gamepad action, so a scripted
  test needs gamepad input or a verified way to inject the `Parry` action.
- **Unknown:** what a counter does to the attacker; whether keyboard `F`
  counters at all on PC; whether `SendInput` reaches the game in the lab.
- **Cleanup:** lab clone destroyed after each run; base game copy never
  written. Archives: `<archive>/anim-mirror-20261107-1043/` through
  `<archive>/anim-mirror-20261107-1106/`.

## Counter test: keyboard and mouse input against the source and the stand-in (2026-11-07, one PC, one game)

- **Question:** is there a keyboard or mouse counter, and does lab input
  reach the game? This follows the failed scripted counters above.
- **Build:** worktree `anim-mirror-probe`, game build 19221447. New
  `anim-mirror-pawn` options: `watch-counter` logs the counter and melee
  events and counts every notify on the player's anim without pressing
  anything; `counter-click` clicks the left mouse button with `SendInput`
  0.15 s after a counter window opens. The lab run script can hold a key
  (or click) through xdotool every 0.6 s during the stand phase.
- **Setup:** stand-still scenario, `god-off`, options
  `move-request,state,play-oneshot,heal,mute-gameplay` plus the options
  below; the stand-in-only run also used
  `mute-source-counter,no-offset,counter-for-target`.
- **Result:** FAIL for the counter: no `OnCounterAttackSuccessful` and no
  `AnimNotify_StartedCounter` in any run. PASS for lab input: xdotool and
  `SendInput` both reach the game.
- **Observed:**
  - xdotool `F`, 104 holds over 2 runs: no swing or other player anim notify
    besides damage, grapple and movement. A screenshot pair shows the HUD's
    `F` quick slot changing after one hold, so `F` reached the game; on
    keyboard it appears to select the melee weapon rather than swing.
    `space` (52 holds) showed no roll notify.
  - Full `InputSettings` dump: `WeaponFire=LeftMouseButton`; `Parry` stays
    gamepad-only and no action names `Counter` or `Dodge`.
  - xdotool left click, 54 clicks: 11 `AnimNotify_MeleeSwingHit_*` swings
    on the player's anim.
  - `counter-click` against the source, 2 runs, 22 clicks: each click
    swung (`OnCanDoCoolMeleeAttackEnded`) and the source lost 15 health per
    hit, down to its death (`AnimNotify_MeleedDeathBlow`) in both runs.
    `AnimNotify_FinishCounter` fired on the player's anim at the press 4
    times, never with a `StartedCounter` before it.
  - `counter-click` in the stand-in's window only (no offset), 2 clicks:
    the swing hit the source (156.2 to 141.2) while the stand-in stayed at
    200. The run ended in a grapple kill.
- **Derived:** keyboard and mouse play has no counter binding here; the left
  click is an ordinary melee swing, which also interrupts the attack. The
  counter needs the gamepad `Parry` button. The earlier `SendInput F`
  failure was the wrong key, not lost input.
- **Unknown:** what a gamepad counter does to the attacker; whether a
  click against an offset stand-in damages it (no clicks landed on it);
  what `FinishCounter` without a start means.
- **Cleanup:** lab clone destroyed after each run; base game copy never
  written. Archives: `<archive>/anim-mirror-20261107-1504/`,
  `-1510/`, `-1514/`, `-1520/`, `-1524/` and
  `<archive>/input-check-20261107-1517/`.

## Pawn mirror: hit reactions, death and looping one-shots (2026-11-07, one PC, one game)

- **Question:** how does an enemy take hits and die, what can kill the
  stand-in on a guest, and do looping one-shots need replaying?
- **Build:** worktree `anim-mirror-probe`, game build 19221447. New
  `anim-mirror-pawn` options: `watch-death` logs damage and death events on
  the source and the stand-in; `kill-clone` kills the stand-in 12 s in
  (`GameplayStatics.ApplyDamage` of its health + 10, then
  `BendPawn.ForceKill(null, false)` if it is still alive); `loop-oneshot`
  replays a one-shot whose montage ended while the source still plays it.
- **Setup:** stand-still scenario, `god-off`, options
  `move-request,state,play-oneshot,heal,mute-gameplay`; the death trace also
  used `counter-click` so the player beat the source to death.
- **Result:** PASS for the death path: `ForceKill` kills the stand-in
  natively. Looping and hit reactions: partial (below).
- **Observed:**
  - A player hit on the source arrives as `ModifyDamage`,
    `ReceivePointDamage` and `ReceiveAnyDamage` (15.0 by the player
    controller, causer `Bend_WpnMelee_BootKnife_v2_C_0`), then the source
    enters `Damage` (15); all 20 forwarded `Damage` states in 2 runs were
    accepted on the stand-in. The swarmer's hit animations are
    blend spaces (`Swarmer_Damage_Head_Light`), so they are not replayed as
    montages.
  - When the source died (`OnDied_Event`, `HandleOnDied`), the stand-in got
    `ReceiveAnyDamage 210.0` with no instigator or causer 0.22 s later and
    died too (health −10, `OnDied_Event`, `Dead_Entered`, ragdoll). The
    same happened in both earlier runs where the source died.
  - `kill-clone`: `ApplyDamage(210)` returned 0.0 and took the stand-in
    only from 200 to 179 (`bCanBeDamaged=1`); `ForceKill` then took it to
    −10, and `AnimNotify_Dead_Entered` followed.
  - `loop-oneshot`, 2 runs: no replay was needed. The grapple loop
    `swm_b_grapple_01_loop_105frames_swm` was seen as a new one-shot each
    time it restarted, and one-shot samples matched 36 of 36 and 7 of 7; a
    baseline run matched 37 of 38. The earlier fall-back to idle did not
    recur.
  - Grapple kills of the player ended 3 of 4 runs early (3.6–23 s).
- **Derived:** an "enemy died" event on the guest can call `ForceKill` on
  the stand-in, which runs the game's own death (−10 health, matching the
  native kill). Damage applied with `ApplyDamage` is scaled by the game, so
  it cannot carry an exact health value. Hit reactions follow from the
  forwarded `Damage` state. What killed the stand-in when its source died is
  not traced; it is probably tied to the cloned source and would not happen
  on a real guest.
- **Unknown:** whether the stand-in's hit-reaction blend space matches the
  source's hit direction; the visual death on a guest (in the manual list);
  whether a longer grapple loop still falls back to idle; other enemy types.
- **Cleanup:** lab clone destroyed after each run; base game copy never
  written. Archives: `<archive>/anim-mirror-20261107-1533/`, `-1538/` and
  `-1545/`.

## Pawn stand-ins over the wire: first paired run (2026-11-07, one PC, two games)

- **Question:** does the pawn mirror work between two games: a host enemy
  shown on the guest as a pawn of its own class, fed from packets?
- **Build:** worktree `anim-mirror-probe`, protocol v13, game build
  19221447, `[Research] PawnEnemies=1` and `DebugTools=1` on both. New
  datagrams: `DMMO` (112 bytes per enemy at 15 Hz: position, yaw,
  velocity, acceleration, the AI move request, movement mode, anim state,
  health, alive) and `DMEV` (class, one-shot with an "aimed at you" flag,
  died; sent twice, deduplicated). New `src/coop/standin.cpp` reads the
  owner's pawn and spawns and feeds the receiver's stand-in; gameplay
  notifies on stand-ins are muted, counter notifies pass only for attacks
  aimed at the local player. The host still sends bone poses too.
- **Setup:** paired lab (`pair.sh`), host save shared; both `begin` and
  `god-on`; host `spawn-single` and `probe-enemy`; four guest and host
  screenshots 8 s apart; three guest `hit-enemy`; host `kill-owned-enemy`.
- **Result:** PASS for spawn, attacks, hits and death in one run; one
  side effect found (below).
- **Observed:**
  - The guest spawned the stand-in (`stand-in 1 is a SwarmerPawn_C`) and
    played 41 of the host's 59 one-shots. The rest (`swm_atk_left_165`,
    `swm_atk_right_165`, `swm_atk_right_110`, `swm_atk_left_110`) were
    reported as not loaded and requested again about every 10 s.
    *Correction:* they were loaded; the guest's lookup could not match
    names ending in digits (see "the missing sequences were a name lookup
    bug" below).
  - Host: 15.0 `DMMO` per second for one enemy; total enemy traffic stayed
    16–18 kB/s because the poses are still sent.
  - Screenshots at the same moment: on the host the swarmer grapples the
    host; on the guest the stand-in grapples the host's avatar in the same
    spot.
  - All 3 guest `hit-enemy` hits reached the host's swarmer (156.2 to
    100.0).
  - Host `kill-owned-enemy` at 17:45:33.12; the guest logged `stand-in 1
    died` at 17:45:33.14, and the replica expired 12 s later after the
    host's cleanup.
  - **Side effect:** right after the stand-in's `ForceKill`, the guest's HUD
    showed "+1 SWARMER EAR": the guest's game credited the kill and its loot
    to the guest. The corpse was not in frame in the following screenshot.
- **Derived:** the stand-in path works between two games for a swarmer:
  spawning by class, movement, attacks and grapples, hits and death.
  `ForceKill` with no killer still runs the game's kill reward for the local
  player, so a guest-side death needs another path or the reward
  suppressed. (This entry also concluded that one-shot sequences needed to
  be loaded and held on the guest; that was the lookup bug, corrected
  below.)
- **Unknown:** the bandwidth with poses turned off; more than one enemy;
  other enemy types; the corpse and ragdoll on the guest; whether the
  stand-in ever damaged the guest (both were in god mode); two-PC timing.
- **Cleanup:** both lab clones destroyed; base game copy never written.
  Archive: `<archive>/pawn-20261107-1741/`.

## Pawn stand-ins: dropping poses and a pack of eight (2026-11-07, one PC, two games)

- **Question:** can the host stop sending bone poses for enemies the guest
  shows as stand-ins, and does a death avoid the guest's kill reward?
- **Build:** worktree `anim-mirror-probe` commit `099697d`, protocol v13.
  The guest sends a `DMEV` stand-in acknowledgement per live stand-in about
  once a second; the host skips the bone poses of a living enemy
  acknowledged within 2.5 s. On a death event, or an update without the
  alive flag, the guest removes the stand-in and the poseable replica takes
  over from the host's corpse poses. One-shot sequences are added to the
  root set once played (bit 30, checked at run time against rooted classes
  and unrooted actors).
- **Setup:** paired lab as in the previous entry, but `spawn-pack` (8
  swarmers); a second run with `PawnEnemies=0` on both as the baseline.
- **Result:** PASS for pose dropping, eight stand-ins and the death
  hand-over. One-shots and holding were not exercised (below).
- **Observed:**
  - The guest spawned 8 `SwarmerPawn_C` stand-ins within 20 ms, and the
    host logged `poses paused` for all 8 within 12 ms of each other.
  - Host enemy traffic with stand-ins: 0 pose packets and 16.2–17.1 kB/s
    over six 5 s windows (8 × 15 `DMMO` per second plus infos and events).
    Baseline without stand-ins: 239–242 pose packets per second and
    103.1–103.8 kB/s. Poses resumed (17.6, then 30 per second) for the
    corpse after the kill.
  - All 3 guest `hit-enemy` hits reached the host in both runs.
  - After `kill-owned-enemy` the guest logged `stand-in 3 died; showing the
    owner's corpse`, and its screenshot showed no kill-reward notice.
  - The pack spawn opened the "Fight or flight" tutorial on the host, which
    paused only the host's game (roadmap item 3). The frozen swarmers made
    1 one-shot in the whole run, so the root-set check never ran.
  - Guest screenshot: eight stand-ins standing around both players.
- **Derived:** puppets cut enemy traffic by about 6× for eight swarmers,
  with the corpse still exact from poses. A guest-side death without
  `ForceKill` avoids the game's kill reward.
- **Unknown:** more than eight enemies; two-PC timing. (One-shot holding
  and its root-set check were later removed as unnecessary.)
- **Cleanup:** both lab clones destroyed after each run; base game copy
  never written. Archives: `<archive>/pawn-20261107-1755/` and
  `<archive>/pawn-20261107-1800/`.

## Pawn stand-ins over Steam: first two-PC run (2026-11-07, two PCs, Steam)

- **Question:** do pawn stand-ins work between two PCs over the Steam
  transport, with real timing and the poses paused?
- **Build:** commit `29e7a5e` (protocol v13; ASI `75cbc641…`), game build
  19221447. After `twopc.sh prep` both PCs got an INI with only
  `DebugTools=1` and `[Research] PawnEnemies=1`.
- **Setup:** a KDE Plasma desktop PC hosting, a Steam Deck in game mode
  joining with JOIN A FRIEND; host save transferred to the guest's sandbox
  (7.5 MB). Both `begin` and `god-on`; host `spawn-pack` (8 swarmers), the
  "Fight or flight" tutorial dismissed on the host; screenshots from both;
  three guest `hit-enemy`; host `kill-owned-enemy`.
- **Result:** PASS for the stand-ins over Steam, the pose pause, hits and
  the death hand-over; FAIL for one-shot animations (below; the cause was
  a lookup bug, fixed afterwards).
- **Observed:**
  - The Deck spawned 8 `SwarmerPawn_C` stand-ins within 62 ms; the host
    paused poses for all 8 within 50 ms.
  - Host enemy traffic for 2 min 40 s with 8 enemies: 0 pose packets and
    16.2–17.1 kB/s (120 `DMMO` per second). After the kill the corpse's
    poses resumed at 30 packets per second, 29.5 kB/s in total.
  - Guest screenshots showed the stand-ins around the guest and moving and
    attacking during the fight.
  - One-shots: the host sent 129, the Deck played 78 and reported 51 as
    not loaded. *Correction:* all 51 were names ending in digits, which the
    lookup could not match; they were loaded (see "the missing sequences
    were a name lookup bug").
  - The root-set check refused: bit 30 was set on 2945 of 2945 native
    classes and on 142 of 3907 actors, so no sequence was held.
  - All 3 guest hits reached the host's swarmer (160.0 to 103.8); the host
    kill showed on the Deck as `stand-in 3 died; showing the owner's
    corpse`.
  - The host's screen captured black after its pause menu opened; the game
    kept running.
- **Derived:** the stand-in design holds up over Steam between separate
  PCs. Corpses should not keep full-rate poses.
- **Fixes after the run:** a corpse whose stand-in was taken over gets 1
  pose per second after 3 s. (A root-set check for holding sequences was
  adjusted here and later removed with the holding, once the lookup bug was
  found.)
- **Unknown:** whether holding the sequences closes the one-shot gap; how
  the stand-ins look in motion on the Deck (screenshots only); more than
  eight enemies.
- **Cleanup:** the Deck quit through the menu and `restore` matched all 57
  save files, the ASI and the INI. The host PC was rebooted with its game
  still running (its screen captured black); `restore` then matched all 85
  save files, the ASI and the INI.

## Pawn stand-ins: loader experiments for sequences reported as not loaded (2026-11-07, one PC, two games)

- **Question:** why a third of the one-shots the host sends are not loaded
  on the guest, and whether holding them fixes it.
- **Build:** worktree `anim-mirror-probe` after `b182bcf`, protocol v13.
  Requested sequences are checked every frame and added to the root set as
  soon as they appear; the root-set check now allows 10% of actors.
- **Setup:** paired lab pack runs as before (`spawn-pack`, the tutorial
  dismissed with one `E` hold), one run per change.
- **Result:** FAIL. The missing sequences never load, so holding cannot
  help.
- **Observed:**
  - The root-set check passed (2945 of 2945 classes, 142 of 4401 actors)
    and was used.
  - `-1847`, streaming-manager loads (`RequestAsyncLoadBPCallback`), held
    only when an event found them: 29 of 82 one-shots missing; the same
    sequences were requested again every 10–20 s.
  - `-1858`, the same with a per-frame check: 26 of 74 missing, and none of
    10 requested sequences appeared within 30 s. (`-1853` broke: a second
    `E` hold made the host use the bike and ended its debug session.)
  - `-1905` and `-1913`, `KismetSystemLibrary.LoadAsset` (latent,
    `Linkage -1`): all 21 loads started, none of the sequences appeared
    within 30 s, and none existed under another outer either (lookup by
    short name). 25 of 70 missing.
  - `-1921`, the stand-in's AI controller kept with its brain stopped: 24 of
    68 still missing.
  - The missing ones are always the same group: `swm_atk_*_110/165/230`,
    `swm_atk_run_520` and `swm_f_grapple_jump_125/250`. Others (such as
    the grapple loops and kills) played.
  - Corpse poses with the 1 Hz rule: 6.0–12.8 pose packets per second for
    the first seconds after a death, then 2.0.
- **Derived (corrected by the next entry):** this run concluded that the
  sequences could not be loaded. They were loaded all along; the lookup
  that checked for them could not match names ending in digits.
- **Cleanup:** lab clones destroyed after each run. Archives:
  `<archive>/pawn-20261107-1847/` through `<archive>/pawn-20261107-1921/`.

## Pawn stand-ins: the missing sequences were a name lookup bug (2026-11-07, one PC, one and two games)

- **Question:** where the host's missing attack sequences live and what
  references them (follow-up to the previous entry).
- **Build:** worktree `anim-mirror-probe`, new debug command `asset-refs
  --arg NAME`: every live object of that name with its full path, flags and
  outers, and every object whose reflected fields or array elements point
  to it.
- **Setup:** one lab instance, `asset-refs` on three missing sequences
  before any fight and after a pack fight; then the paired pack run.
- **Result:** PASS. Cause found and fixed: 96 of 96 and 55 of 55 one-shots
  played on the guest in two paired runs.
- **Observed:**
  - First `asset-refs`: "no live object named swm_atk_right_165", even on
    the host after a fight.
  - Unreal keeps a trailing `_<digits>` of a name as the FName number
    (`swm_atk_right_165` is `swm_atk_right` number 166). `ue::Name()`
    prints it back, but `ue::FindObject` looked up the whole text and
    required number 0. Every missing sequence ended in digits; every
    played one did not.
  - With the split in `FindObject`: `swm_atk_right_165` and
    `swm_atk_left_110` are held by the data asset
    `/Game/Animation/SwarmerB/SwarmerAttacks` (`BendAttackAnimData`,
    `AttackAnimsList`), `swm_f_grapple_jump_250` by the blend space
    `swm_f_grapple_jump_distance` (`SampleData`). All three were loaded
    before any fight.
  - Paired pack runs `pawn-20261107-1950` and `-1956`: one-shots 96 sent,
    96 played and 55 sent, 55 played; host enemy traffic 16.2–17.0 kB/s
    with 8 stand-ins.
- **Derived:** the sequences were never unloaded; the loader experiments in
  the previous entry measured a lookup that could not succeed. The root-set
  holding and the latent loader were removed again. Any other lookup of a
  name ending in digits through `FindObject` was affected the same way.
- **Audit of earlier findings:** `FindClass` had the same bug, and
  `FindChild` (properties and functions) ignored the FName number entirely,
  so `"Foo"` could match a field `Foo_0`; both now match the number too. No
  hard-coded asset path, hook name or name literal in the code ends in an
  FName number. Of the 18 distinct asset paths the mod requested across all
  lab archives and the 2026-11-07 two-PC logs, the 12 ending in a number
  are exactly the swarmer sequences above; the companion meshes and
  materials (Boozer) do not, so the companion, vehicle and avatar entries
  stand. The entries corrected are this day's stand-in runs.
- **Cleanup:** lab clones destroyed. Archives: `<archive>/refs-20261107-1943/`,
  `<archive>/refs-20261107-1947/`, `<archive>/pawn-20261107-1950/`,
  `<archive>/pawn-20261107-1956/`.

## Pawn stand-ins: lifting the 16-enemy cap (2026-11-07, one PC, two games)

- **Question:** can more than 16 enemies be streamed once they are pawn
  stand-ins, and what does 16 cost?
- **Build:** worktree `anim-mirror-probe` after `85a3d06`, protocol v13.
  With `PawnEnemies` active the scan keeps up to 64 enemies
  (`kEnemyMaxStandIns`); only the nearest 16 may fall back to bone poses;
  stand-ins beyond 30 m get a third of the motion updates; an acknowledged
  stand-in's `DMEN` info goes every 5 s instead of every second; the
  receiver keeps up to 72 replicas.
- **Setup:** paired lab, three `spawn-pack` on the host, the tutorial
  dismissed, then the usual hits and kill.
- **Result:** PASS for 16; more than 16 could not be tested here.
- **Observed:**
  - The third `spawn-pack` was refused: "Test spawn limit is 16 owned
    enemies". The host streamed 16.
  - Host enemy traffic with 16 stand-ins: 0 pose packets and 27.7–28.1
    kB/s; 18.4 then 2.0 pose packets per second for the corpse.
  - One-shots: 39 sent, 39 played.
  - Tick peaks: guest 140–150 ms and host 40–44 ms in the last windows. The
    `PawnEnemies=0` baseline with 8 enemies (`pawn-20261107-1800`) had
    guest peaks of 115–122 ms and host 30–33 ms, so the guest's peaks are
    mostly the one-PC lab (two games on one GPU), not the stand-ins.
- **Derived:** 16 stand-ins cost about 1.7 kB/s each; 64 would be about
  110 kB/s. Testing past 16 needs a natural swarm or horde, not the debug
  spawner.
- **Follow-up:** as first written, a corpse ranked beyond the nearest 16
  got no poses at all and would have vanished on the guest; it now gets one
  pose a second from its death. A regression run with one pack
  (`pawn-20261107-2031`) played 96 of 96 one-shots, at 13.9 kB/s for 8
  stand-ins with the 5 s info interval. The far-corpse path itself needs
  more than 16 enemies and is untested.
- **Unknown:** more than 16 enemies; a horde's frame time on the guest;
  the Steam send budget at 64 stand-ins; two-PC timing at 16.
- **Cleanup:** lab clones destroyed. Archive:
  `<archive>/pawn-20261107-2021/`.

## Enemy scan: pools, hordes and the 2026-11-06 gap (2026-11-07, one PC, one and two games)

- **Question:** why the host streamed no enemies in the 2026-11-06 two-PC
  run while a swarm attacked (roadmap item 2).
- **Build:** worktree `anim-mirror-probe` after `3743b4f`. New debug command
  `crowd-probe`: crowd actors and components by class with their distance;
  every `BendAIPawn` with its controller and the scan rule that takes or
  rejects it; positions of `BendCrowdController`s and horde pawns as
  `move-by` offsets.
- **Setup:** one lab instance at the base save's location (the same place
  as the 2026-11-06 run: about -30000 -206000): at the start, 182 m from
  the horde pool, near the pooled swarmers, three minutes at night
  (`time-shift` twice), and after `spawn-single`.
- **Result:** the gap was not reproduced (no active natural enemy appeared);
  the scan's pool rule was found wrong for horde pawns and replaced.
- **Observed:**
  - A horde pool: 8 `HordeSwarmerPawn_C` (hidden, health 200, **no
    controller**), 8 `BendCrowdController` actors and 988
    `BendCrowdSharedAnimSkeletalMeshComponent` crowd members, all at one
    point about 2 km away.
  - An AI pool about 170 m below the player, pawns in a row 1 m apart, each
    with its controller: 15 `SwarmerPawn_C`, 20 `MarauderPawn_C`, 15
    `RatPackPawn_C`, 6 `SurvivorPawn_C`, deer, wolves, crows and newts.
  - Neither pool moved during three minutes at night or after moving 2 km
    toward the horde pool.
  - The 2026-11-06 host log has one `partner stand-in took 40.0`: that
    stand-in is the partner proxy (a `SwarmerPawn_C` on the player's faction),
    so the log does not name the attacker.
  - The old rule skipped every living pawn without a controller as pooled,
    which also matches every horde member promoted to a full pawn (driven by
    a `BendCrowdController` actor, not an AI controller).
  - With the new rule, all 106 pooled pawns were classified "parked in a
    pool" and the debug swarmer was selected; a paired pack regression run
    (`pawn-20261107-2113`) played 166 of 166 one-shots.
- **Change:** `ParkedPawn` replaces the controller test: hidden pawns,
  controllers with `bPooledSpawn` and not `bPooledSpawnInUse`, and
  controller-less pawns that are not freakers are parked. `SharedEnemy`
  accepts a controller-less `BendFreakerPawn` (a horde member). Both apply
  only with `[Research] PawnEnemies=1` until a live horde confirms them;
  normal play keeps the old rule (checked: without the key the probe
  classified the pools exactly as before).
- **Derived:** the most likely cause of the 2026-11-06 gap is that rule:
  a swarm of promoted horde pawns has no AI controllers, so none was
  selected. Not proven: the run's attackers are not named in its log.
- **Unknown:** what activates a pool here; whether active horde pawns are
  visible pawns or only crowd meshes near the player; a test with a natural
  swarm or horde.
- **Cleanup:** lab clones destroyed. Archives: `<archive>/crowd-20261107-2053/`
  through `<archive>/crowd-20261107-2111/`, `<archive>/pawn-20261107-2113/`.

## Spawning natural enemies next to the player (2026-11-07, one PC, one game)

- **Question:** can a debug command put natural (ambient) enemies, or a
  horde, next to the player, to test the enemy scan and more than 16 enemies?
- **Build:** worktree `anim-mirror-probe` after `ead6352`. New debug
  commands: `spawn-types` lists every live `BendAmbientSpawnType` (1056
  bytes; pawn class at +272) by owner and pawn class; `spawn-horde --arg
  "MODE PAWN COUNT"` copies one whose pawn class is exactly `PAWN_C`
  (preferring the world's own) into the world's `BendAmbientGenerator`:
  `arcade` (`SpawnForArcadeMode`, 20 m ahead), `horde` (the same, into a
  live crowd group), `scream` (`ScreamerSpawn`) or `escalate`
  (`SpawnForCombatEscalation` within 40 m, no spawn type).
- **Setup:** one lab instance, `[Research] PawnEnemies=1`, god mode on,
  `crowd-probe` 10 s after each spawn.
- **Result:** PASS for natural enemies with AI controllers; FAIL for horde
  members.
- **Observed:**
  - 573 spawn types: the arcade data (`ArcadeEnemyData.SpawnTypes`, with
    `HordeSwarmerPawn_C` first), the world's
    `AmbientSpawnGenerator_C_0.PooledSpawnTypes`, spawn areas and locations.
  - `arcade SwarmerPawn 10` with the world's pooled type: 2 new swarmers
    52–62 m away, selected by the scan. `escalate` three times: pool
    swarmers and 3 wolves activated, up to 8 enemies selected at once.
  - `arcade HordeSwarmerPawn 20` (arcade data), `scream` (both types) and
    `horde` (into `HordeC252_HordeGroup` of the world horde level
    `07-14_HordeC252_LittleBearLake`): nothing appeared within 10 s. No
    crash.
- **Derived:** the debug spawner's 16-enemy limit can be passed with
  `arcade`/`escalate` spawns, which come out of the game's own pools with
  their controllers. The base save sits beside a real horde (Little Bear
  Lake, its group about 144 m from the start), whose members stay parked
  until something wakes the horde.
- **Unknown:** how to wake a horde or spawn horde members; whether active
  horde members are pawns without controllers.
- **Cleanup:** lab clones destroyed. Archives: `<archive>/crowd-20261107-2138/`
  through `<archive>/crowd-20261107-2149/`.

## Pawn stand-ins past 16 enemies (2026-11-08, one PC, two games)

- **Question:** do stand-ins hold up past the debug spawner's 16, with
  natural enemies from the game's own pools?
- **Build:** worktree `anim-mirror-probe` after `3a92d36`, protocol v13. The
  tick's perf line now names its slowest section (`slowest=`), and the
  stand-in line gives the peak lookup, spawn, feed, destroy and per-tick
  stand-in time.
- **Setup:** paired lab, `[Research] PawnEnemies=1` on both, god mode. Host:
  two `spawn-pack`, then `escalate`, `arcade SwarmerPawn 10`, `escalate`,
  `arcade SwarmerPawn 10`, `escalate`, 6 s apart; `crowd-probe` every 8 s
  after; then the usual hits and kill on the guest. Five runs, the last two
  with the finer timing and the fix.
- **Result:** PASS for streaming past 16 (up to 35 enemies at once); a
  guest frame hitch was found and fixed.
- **Observed:**
  - Peak streamed on the host: 35, 31 and 31 enemies in the first three
    runs; up to 39 live
    stand-ins on the guest (replicas still fading out while new ones
    spawned). 0 stand-in spawn failures, 0 missing one-shots.
  - Host enemy traffic: 27.7 kB/s for 16 stand-ins, 31–32 kB/s for 21–24,
    36.9–45.8 kB/s for 27–35 (with corpse poses and joining enemies).
  - The natural enemies left after 15–40 s. `crowd-probe` showed them back
    in the pools (swarmers parked 10 → 15, wolves 0 → 3) as the streamed
    count fell back to the 16 debug swarmers, so the game repooled them; the
    scan did not drop them.
  - Guest tick peaks of 805 ms and 566 ms in the windows where 15 stand-ins
    spawned at once. Timing put it in `Feed`: its first call 0.5 s after the
    spawn scans the whole object array for the pawn's claw weapons (45–60
    ms), once per stand-in, so 15 stand-ins spawned together paid 15 scans in
    one tick. The `SpawnAIFromClass` call itself took 3–8 ms, the class and
    mesh lookups 8–18 ms.
  - After the fix (stand-ins due in the same tick share one scan), 13
    stand-ins spawned in one window peaked at 53.6 ms in stand-in code
    (`pawn-20261108-1116`).
  - The remaining guest peaks (115–170 ms in every window, with or without
    enemies) are `UpdateStory`; the host's are `UpdateStory` (30–45 ms) and,
    while debug commands run, `UpdateDebugTools` (110–190 ms).
- **Derived:** stand-ins scale past 16 at about 1.3–1.7 kB/s each. A burst
  of stand-ins arriving at once still costs one object scan per tick (about
  50 ms) plus per-spawn lookups; this is a one-PC figure with two games on
  one GPU.
- **Unknown:** a real horde (dozens at once); far-corpse poses past rank 16
  (no enemy died beyond the nearest 16); two-PC timing; what in
  `UpdateStory` takes 115–170 ms on the guest.
- **Cleanup:** lab clones destroyed. Archives: `<archive>/pawn-20261108-1045/`,
  `<archive>/pawn-20261108-1052/`, `<archive>/pawn-20261108-1101/`,
  `<archive>/pawn-20261108-1109/`, `<archive>/pawn-20261108-1116/`.

## Frame peaks from the story sample (2026-11-08, one PC, two games)

- **Question:** what makes the 115–170 ms guest tick peaks that appear in
  every 5 s window, with or without enemies?
- **Build:** worktree `anim-mirror-probe` after `f5663b5`, with a debug log of
  the story read's parts (`perf: story read`, at most every 30 s when over
  20 ms).
- **Setup:** paired lab, `[Research] PawnEnemies=1`, one `spawn-pack`, the
  usual hits and kill; before and after the fix.
- **Result:** PASS; the peaks were an uncached object lookup, now cached.
- **Observed:**
  - Before (`pawn-20261108-1135`): `slowest=UpdateStory` in 24 of the
    guest's windows (up to 146.5 ms) and 32 of the host's (up to 70.3 ms).
    Each 3 s story read took 23–43 ms, nearly all in the active-objective
    snapshot, whose `FindInstance("BendJobManager")` scanned the whole object
    array on every call; the guest's objective-sync apply looked the manager
    up the same way for each host objective packet.
  - After (`pawn-20261108-1141`), with `FindInstance` keeping its instance
    while it stays live: one slow story read per game (the first, 52 and 86
    ms); `UpdateStory` was no window's slowest section after that. Tick peaks
    fell from 121–136 ms to 15–31 ms on the guest and from 31–38 ms to 17–19
    ms on the host. One-shots and the kill behaved as before.
- **Derived:** this cost every player a 25–40 ms frame each 3 s whenever a
  partner is connected, more on the guest; it was not enemy related.
- **Unknown:** two-PC frame times with the fix.
- **Cleanup:** lab clones destroyed. Archives: `<archive>/pawn-20261108-1135/`,
  `<archive>/pawn-20261108-1141/`.

## Pawn stand-ins over Steam past 16 enemies (2026-11-08, two PCs, Steam)

- **Question:** do 16+ stand-ins, the spawn-burst fix and the story-sample fix
  hold on two PCs over Steam, with a Steam Deck as the guest?
- **Build:** worktree `anim-mirror-probe` at `4809915`, protocol v13, on the
  host PC and a Steam Deck through `tools/twopc` (`prep`/`restore`, backup
  label `20261108-steam`), INI `DebugTools=1` and `[Research]
  PawnEnemies=1` on both.
- **Setup:** Steam lobby, the guest joined and loaded the host's save
  (transfer 36 s, progress verified). Both god mode. Host: two `spawn-pack`,
  then `escalate`, `arcade SwarmerPawn 10`, `escalate`, `arcade SwarmerPawn
  10`, `escalate`, 6 s apart, three `crowd-probe`; guest: three `hit-enemy`;
  host: `kill-owned-enemy`; both quit through the menu.
- **Result:** PASS.
- **Observed:**
  - The host streamed 21 enemies (18 swarmers and 3 wolves; the natural ones
    stayed for the whole two minutes this time). The Deck spawned 21
    stand-ins, 0 failures, and showed the swarm around both players.
  - Host enemy traffic: 26.7–31.4 kB/s for 16–21 stand-ins, 39.1 kB/s in the
    window with the kill (18 pose packets for the corpse), then 2 pose
    packets per second.
  - All three guest hits reached the host's pawn (25 damage each, 160 →
    103.8); the kill showed the host's corpse on the Deck. One-shots: 4
    sent, 4 played.
  - Deck tick peaks 19–26 ms outside spawn bursts, with no `UpdateStory`
    peaks (the lab had 120–170 ms before the story fix). Host peaks 8–22 ms,
    except 120–184 ms in `UpdateDebugTools` while spawn commands ran.
  - Spawn bursts on the Deck: 70–77 ms in stand-in code when 8 or 3 stand-ins
    arrived together, the first `Feed`'s weapon scan (shared since
    `f5663b5`); lookup peaks 12–21 ms, spawn 6–32 ms.
  - The host's "Fight or flight" tutorial was open after the fight; the Deck
    was not paused (roadmap item 3).
  - `restore` on both: 85 and 57 save files, ASI and INI matching the backup.
- **Derived:** stand-ins work on a Steam Deck over Steam past the old cap,
  at about 1.5 kB/s per enemy. Each arriving group of stand-ins still costs
  one object scan on the receiver (about 70 ms on the Deck), which a direct
  weapon lookup would remove.
- **Unknown:** a real horde; 30+ enemies on two PCs; far-corpse poses.
- **Cleanup:** both PCs restored and verified; post-test logs and saves are
  in each PC's `~/TheysGone-backup-20261108-steam/after/`.

## Pawn stand-ins: finding a stand-in's weapons without an object scan (2026-11-08, one PC, two games)

- **Question:** can the stand-in's claw weapons be found from the pawn
  itself, so a group of new stand-ins no longer costs a full object scan
  (about 70 ms on the Steam Deck in the
  [two-PC run](#pawn-stand-ins-over-steam-past-16-enemies-2026-11-08-two-pcs-steam))?
- **Build:** worktree `anim-mirror-probe` after `6735e04`. The weapons come
  from `BendPawn.EquippedWeapons` and `Actor.Children` (both arrays of
  objects, read from the reflected properties); the object scan only runs
  when both are empty. With `DebugTools=1` the first three stand-ins also
  count their weapons with the scan, logged as `stand-in N weapons: A from
  its lists, B by object scan`.
- **Setup:** paired lab, the same spawn bursts as the
  [lab run past 16](#pawn-stand-ins-past-16-enemies-2026-11-08-one-pc-two-games)
  (`pawn-20261108-1226`).
- **Result:** PASS.
- **Observed:**
  - All three checked stand-ins: 3 weapons from its lists, 3 by object scan.
  - First-update (`Feed`) peaks after those checks: 0.2–1.9 ms, including a
    burst of 14 stand-ins (before: 45–77 ms per burst). The first burst's
    51.9 ms is the debug check's own scan.
  - The burst of 14 still peaked at 44.8 ms for all stand-in work in one
    tick: 14 spawns at up to 2.8 ms and class and mesh lookups at up to 8.3
    ms.
  - 197 one-shots played, 0 missing; 0 spawn failures.
- **Derived:** the remaining cost of a burst is the spawn and lookups, about
  3 ms per stand-in in the lab.
- **Unknown:** the same on the Steam Deck; whether other enemy types (human
  enemies with guns) keep their weapons in the same lists.
- **Cleanup:** lab clones destroyed. Archive: `<archive>/pawn-20261108-1226/`.
