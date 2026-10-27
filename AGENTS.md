# AGENTS.md

Guidance for AI coding agents and human contributors working on Theys Gone,
a two-player co-op ASI plugin for Days Gone (PC, Steam build 19221447,
UE4 BendGame 4.11). Read this first, then the linked docs for the area you
touch.

## Commands

```sh
poetry install   # Python tooling (never create your own venv)
make test        # C++ protocol/motion/combat tests + Python unit tests
make build       # package/TheysGone.asi + TheysGone.ini + branding pak
make dist        # dist/TheysGone-<version>.zip + .sha256
git diff --check
```

The build needs MinGW-w64 (`x86_64-w64-mingw32-g++`, GCC 13+, posix
threads); see [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md#prerequisites). Run
`make test build` before every push. CI runs the same targets.

## Where to read

| Need | Document |
|---|---|
| Design, packets, features | [docs/HOW-IT-WORKS.md](docs/HOW-IT-WORKS.md) |
| Build, source layout, debug tools, releases | [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) |
| Current state, environment, gotchas, code map | [docs/REPLICATION-HANDOFF.md](docs/REPLICATION-HANDOFF.md) |
| Evidence log for every live test | [docs/REPLICATION-RESEARCH.md](docs/REPLICATION-RESEARCH.md) |
| User-facing behavior and settings | [README.md](README.md) |

Most gameplay logic is in [`src/coop/coop.cpp`](src/coop/coop.cpp). Pure,
testable rules live in headers (`enemy.hpp`, `protocol.hpp`, `streaming.hpp`,
`story.hpp`, `save_policy.hpp`) and are covered by
[`tools/test_protocol.cpp`](tools/test_protocol.cpp). Engine replication
experiments live only in [`src/coop/research.cpp`](src/coop/research.cpp)
behind `[Research]` INI keys; they are not part of normal play.

## Current state (protocol v9)

Validated in paired live tests (two PCs, two games):

- Peering (UDP, experimental Steam lobby), partner avatar and animation,
  held items, motorcycles and passengers, appearance, friendly fire, time
  and weather.
- Host-first save transfer, guest save sandbox and story/objective sync
  (`DMST`); see the README for which parts are still marked experimental.
- Shared enemies: the host owns enemies near both players; the guest renders
  replicas and its hits are applied on the host.
- Free roam (`FreeRoam=1`, off by default): the host's dual-focus streaming
  hook keeps the guest's region loaded. Above 250 m apart, each game owns
  enemies near its own player; under 150 m the host owns new enemies again.
- Gradual enemy ownership handoff (protocol v8): one owner per enemy, hits go
  to the owner in either direction, and enemies already fighting the guest
  stay with their owner across a split or regroup until they die, lose
  interest for 6 s, or hit 180 s.

Implemented but **not yet validated live**:

- Protocol v9 guest-side host proxy: lets guest-kept enemies target and
  damage the host after a regroup. The only paired attempt was inconclusive
  because the guest's debug pack never entered the streamed enemy sources
  (see "Protocol-v9 guest-proxy attempt" in REPLICATION-RESEARCH.md).

Known gaps, roughly in priority order:

1. Validate the v9 proxy: confirm guest enemy sources stream and the host
   shows replicas before the regroup, then verify proxy targeting on the
   guest and a host health decrease.
2. Interactions, pickups and mission state in the guest's region are not
   host-authoritative while separated.
3. Enemy identity is not carried over when ownership is released.
4. Unexplained 600–800 ms frame-time peaks while paired.
5. Full engine replication (guest as thin client) remains research only.

Update this section and the docs above whenever the state changes.

## Rules

- **Evidence over claims.** Mark a feature validated only after a paired
  in-game test, and record the evidence (log lines, numbers, archived log
  names) in REPLICATION-RESEARCH.md. Say plainly what was not tested.
  Unit tests do not replace game tests.
- **Build-specific offsets.** Addresses and offsets target Steam build
  19221447 only. Find them with the reflection layer or the tools in
  `tools/re/`; never guess.
- **Protocol changes.** Any change to a packet layout or its meaning bumps
  `kProtocolVersion` in `src/coop/protocol.hpp` (or the datagram's own
  version), updates `tools/test_protocol.cpp` and `VERSION` in
  `tools/fake_peer.py`, and uses a breaking commit (`feat!:`).
- **Live testing.** Never touch a player's real game install or saves. Use
  isolated copies, back up the ASI, INI and saves first, archive logs
  outside the repo, and restore and verify hashes afterwards. Debug
  commands require `DebugTools=1`; see DEVELOPMENT.md "Debug tools".
- **Never commit** game files, saves, logs, credentials or local token files
  (`apitoken`, `sshpw`). The only binary asset is the project's own
  `assets/branding/` pak.
- **Commits** follow Conventional Commits; release-please derives versions
  and the changelog from them. Don't edit `version.txt`, `CHANGELOG.md` or
  `.release-please-manifest.json` by hand.
- **Python** is managed by Poetry; use `make` targets or `poetry run`.
- Keep comments for non-obvious engine behavior only.
