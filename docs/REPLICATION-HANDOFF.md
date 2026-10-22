# Engine replication: handoff

Handoff for continuing the built-in Unreal replication investigation in
another tool or session. Written 2026-10-22. Read this file first, then
[REPLICATION-RESEARCH.md](REPLICATION-RESEARCH.md), which holds the full
evidence, the Phase 1 log and the test plan.

## Prompt to paste

```text
You are continuing a reverse-engineering investigation in the Theys Gone repo
(a two-player co-op ASI mod for Days Gone, Unreal Engine 4.10 Bend fork, Steam
build 19221447). Goal set by the user: full engine replication with the guest
as a thin client, because only that gives a seamless experience. The current
shipped mod uses its own datagram layer plus a host save transfer.

Read, in this order:
1. docs/REPLICATION-HANDOFF.md (state, environment, gotchas, code map)
2. docs/REPLICATION-RESEARCH.md (evidence, Phase 1 log, the two-run test plan)
3. src/coop/research.cpp (all experiment code: net-probe, net-listen,
   net-connect, net-possess, net-takeover, hooks)

State: the engine's own SteamNetDriver connection between two real PCs works
and stays up, with the guest keeping its own Bend controller (hybrid). The
host's GameState, PlayerStates, Deacon pawn and bike replicate to the guest.
Next is the decisive thin-client test: the host spawns a Deacon for the guest
(net-possess) and the guest drives it (net-takeover). Run it twice: run A with
[Research] ControllerClass=PlayerController, run B with ControllerClass empty
(Days Gone's own controller). Only run B can decide whether a thin client is
viable; missing HUD/camera in run A does not count.

Rules: build with `make build`, test with `make test` (Poetry project, never
create a venv). Test PCs are reached with tools/re/rssh (expect wrapper that
reads the git-excluded sshpw file). The remote shells may be fish: wrap remote
commands in bash -c '...'. Ask the user before touching the PCs; they may be
offline or in use. Do not commit unless asked. Record every finding in
docs/REPLICATION-RESEARCH.md as you go.
```

## Where things stand

| Item | State |
|---|---|
| Host listen server inside the live world | works (`net-listen`) |
| Guest join without a map reload | works (`net-connect`, LoadMap hook) |
| Connection stays open | works, verified 60 s+ with both games at the title menu |
| Guest keeps its own controller and Deacon | works (HandleClientPlayer wrapper) |
| Engine replicates to the guest | host GameState, 2 PlayerStates, host Deacon, host bike (simulated proxies) |
| Host in game while listening | never tested |
| Host enemies replicating | not seen at the menu; untested in game |
| Guest pawn on the host (`net-possess`) | coded, never run |
| Guest drives host-owned pawn (`net-takeover`) | coded, never run |
| Which streaming RPC really closed the connection | unknown; both directions are dropped |

All of this is committed on the branch `research/engine-replication`
(not pushed). Another session's README and DEVELOPMENT.md edits about the
save editor are left uncommitted in the working tree. `make test` passes.

## Test environment

| Machine | Access | Role | Notes |
|---|---|---|---|
| `sebastiaan@cachy` | `tools/re/rssh ssh sebastiaan@cachy bash -c "'...'"` | host | CachyOS, Proton, fish shell. Steam account evaclaessen, SteamID64 `76561198264177393` |
| `deck@192.168.1.178` | `tools/re/rssh ssh deck@192.168.1.178 bash -c "'...'"` | guest | Steam Deck. Steam account DrSkunk, SteamID64 `76561197999558802`. Goes offline when it sleeps |

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
| `src/coop/research.cpp` `ProbeNetwork` | `net-probe`: drivers, connections (`CONN`), controllers (`PC`), roles, `PROXY` actors, `CPF_Net` counts |
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
  cachy: `TheysGone-reflection.txt` in the game folder.

## Next steps

Follow the two-run table in [REPLICATION-RESEARCH.md](REPLICATION-RESEARCH.md),
section *Next steps*. It needs both PCs for about 45 minutes, with the user at
the Deck for the visual checks after `net-takeover`.
