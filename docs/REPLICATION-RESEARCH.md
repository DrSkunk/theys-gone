# Built-in Unreal replication: feasibility research

Status: investigation in progress (2026-10-21). Nothing here is shipped; the
mod still uses its own datagram layer described in [HOW-IT-WORKS.md](HOW-IT-WORKS.md).

Question: could Theys Gone use Unreal's own listen-server replication
(`UNetDriver`, actor channels, property replication, RPCs) instead of, or
underneath, its custom pose/enemy/save streaming? This document records what
the shipped build contains, what is unknown, and the experiment plan.

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

## Probe results (2026-10-21, cachy, main menu)

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

Not yet checked: whether the replicated Deacon renders and moves on the guest
once the host is in game, whether host AI replicates (none did at the menu;
relevance is based on the connection's view target, which is not set yet), and
what Continue does on either side with the net driver attached.

INI knobs for the experiment: `[Research] ConnectUrl=steam.<host id>` on the
guest; `ControllerClass=`, `PawnClass=`, `SpectatorJoin=` on the host. The
probe now also lists `PROXY` actors (Role other than Authority), which on the
guest shows what the host replicates.

## How it would develop

### Phase 1: can a net driver come up at all? (1–2 days, decisive)

Host side done, see the log above: a `USteamNetDriver` listens on the live
world without reloading. Guest side: `ExecuteConsoleCommand("open
steam.<host id>")`, then observe `PendingNetGame`, `Welcome`, the map load,
`PostLogin` and `SpawnDefaultPawnFor` on the host.

### Phase 2: what the engine replicates for free

With a connection up: does the second Deacon appear on the host and the
host's Deacon on the guest? Expected: yes for `ReplicatedMovement`
(both are `Character`s with `bReplicates`), no for animation (AnimBlueprint
variables are not `CPF_Net`), no for health/inventory unless the ShooterGame
leftovers still work. Do AI pawns replicate? Their controllers exist only on
the server, so AI logic becomes host-only for free, which is exactly the
current design goal. The guest's own ambient generators still run unless the
client-side game code checks `HasAuthority()`, which single-player code never
needed to do.

Lever worth testing: `FRepLayout` is built lazily per class from properties
flagged `CPF_Net`. Flipping that bit on selected `UProperty` objects before the
first replication of the class would make the engine replicate extra state
(health, weapon, anim-relevant bools) without any game code change. OnRep
callbacks would not run, but plain values would arrive.

### Phase 3: hybrid model

If Phase 2 works, the likely end state is: engine replication for actor
existence, transforms, health and RPC transport; the current pose stream kept
for skeletons (the engine's montage/anim replication would not cover Bend's
AnimBlueprints); the save transfer kept for world/progression state, which
Unreal does not replicate at all (missions, unlocks, map state are in the
save, not in replicated properties). The custom `DMEH`/`DMPX` hit forwarding
would be replaced by `ServerNotifyHit`-style RPCs if they still function, or
by our own RPC-shaped UFunctions invoked through `ProcessRemoteFunction`.

### Phase 4: second-player gameplay

Each place the game assumes one player (`GetFirstPlayerController`, HUD,
camera, mission triggers keyed to "the" player, the bike, the buddy, saves,
fast travel, cinematics) is a bug to find. This is open-ended and is where
true replication would cost months; the save-sandbox architecture sidesteps it
by giving the guest a complete local world.

## Next steps

The thin-client question is decided by one test: can the guest drive a Deacon
the host owns, with Bend's player code intact? Code for it is ready
(`net-possess`, `net-takeover`); the run order is:

| Step | Where | Action | Watch for |
|---|---|---|---|
| 1 | both | restart the game; host Continues into gameplay, guest stays at the title menu | |
| 2 | host | `net-listen` (INI: `ControllerClass=PlayerController`, `SpectatorJoin=1`) | `UWorld::Listen returned true` |
| 3 | guest | `net-connect` | `HandleClientPlayer ... local player kept` |
| 4 | both | `net-probe` | host `ClientConnections=1`; guest `PROXY` list now from a host in game: is host AI there? |
| 5 | host | `net-possess` | host survives a second `BendDefaultPlayerPawn_C`; guest `PROXY` shows `BendDefaultPlayerPawn_C role=2` |
| 6 | guest | `net-takeover` | on the Deck screen: camera on the new Deacon, does it move, HUD, animation |
| 7 | guest | `net-takeover` again | own controller restored |

Run the table twice. Run A uses `ControllerClass=PlayerController` (the
engine's plain controller, known not to crash at join). Run B leaves
`ControllerClass` empty, so the guest gets Days Gone's own
`BendDefaultPlayerController_C`; its host crash happened inside
`RestartPlayer`, which `SpectatorJoin=1` now skips, so run B has not been
tried yet. Read run A's results with care: the HUD, the Bend camera and
controller-bound inputs (focus mode, quick heal, bike exit) live in the Bend
controller, so missing ones in run A say nothing about the thin client.
Movement, animation and the camera following the pawn do count. Only run B
can show whether Bend's player code works on a pawn the host owns.

If step 5 crashes the host, retry with `[Research] GuestPawnClass=Character`
to separate Bend pawn code from the possession itself. To find which
streaming RPC really closed the connection, repeat steps 2 and 3 with
`DropStreamingStatus=0` or `DropLevelVisibility=0`.

After that, depending on the result:

1. Thin client works: move guest AI suppression and host enemies onto engine
   replication, then go system by system (weapons, damage, interaction,
   bike, HUD) to forward guest actions to the host.
2. Thin client breaks Bend's player code everywhere: the hybrid is the
   realistic ceiling; engine replication can still replace the hand-written
   transport for actors and RPCs.

## Current recommendation

Phase 1 is answered: the engine's own net driver runs between two Days Gone
PCs over Steam P2P, inside the live world, with five targeted patches
(definitions, LoadMap, HandleClientPlayer, streaming RPCs, spectator join).
Continue with Phase 2 to measure what the engine replicates for free in game.
Keep the save transfer either way: the engine never replicates missions,
unlocks or map state. The shipped datagram layer stays until engine
replication proves it can carry Deacon and enemies.
