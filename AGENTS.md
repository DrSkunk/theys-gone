# AGENTS.md

Theys Gone is a two-player co-op ASI plugin for Days Gone PC, Steam build
19221447 (UE4 BendGame 4.11). Read the relevant guide before changing code.

| Need | Start here |
|---|---|
| Current status and prioritized next work | [Co-op roadmap](docs/COOP-ROADMAP.md) |
| Player setup and limitations | [README](README.md) |
| Design, packets and engine behavior | [How it works](docs/HOW-IT-WORKS.md) |
| Build, debug tools and releases | [Development](docs/DEVELOPMENT.md) |
| Live-test evidence | [Research log](docs/REPLICATION-RESEARCH.md) |
| Isolated two-game lab | [Lab guide](tools/lab/README.md) |
| Mission catalog and policies | [Story co-op](docs/STORY-COOP.md) |
| Native-replication research | [Research handoff](docs/REPLICATION-HANDOFF.md) |

Most gameplay logic is in `src/coop/coop.cpp`. Pure rules live in headers
such as `enemy.hpp`, `protocol.hpp`, `streaming.hpp`, `story.hpp` and
`save_policy.hpp`, with tests in `tools/test_protocol.cpp`. Engine replication
experiments belong in `src/coop/research.cpp` behind `[Research]` INI keys;
they are not normal play.

## Commands

```sh
poetry install   # Python tooling; never create your own venv
make test        # C++ protocol/motion/combat and Python unit tests
make build       # package/TheysGone.asi, INI and branding pak
make dist        # versioned zip and SHA-256
git diff --check
```

The build requires MinGW-w64 (`x86_64-w64-mingw32-g++`, GCC 13+, POSIX
threads); see [prerequisites](docs/DEVELOPMENT.md#prerequisites). Run
`make test build` before every push; CI runs the same targets.

## Rules

- **Evidence over claims.** Record each live test, its scope and log evidence
  in the research log; update the roadmap when status changes. One-PC paired
  tests and unit tests do not establish two-PC gameplay.
- **Build-specific offsets.** Addresses and offsets target build 19221447
  only. Derive them with reflection or `tools/re/`; never guess.
- **Protocol changes.** Changing a packet layout or meaning bumps
  `kProtocolVersion` in `src/coop/protocol.hpp` (or the datagram's own
  version), updates `tools/test_protocol.cpp` and `VERSION` in
  `tools/fake_peer.py`, and uses a breaking commit (`feat!:`).
- **Live testing.** Never touch a player's real game install or saves. Use
  isolated copies, back up ASI, INI and saves, archive logs outside the repo,
  then restore and verify hashes. Debug commands require `DebugTools=1`; see
  [debug tools](docs/DEVELOPMENT.md#debug-tools).
- **Never commit** game files, saves, logs, credentials or local token files
  (`apitoken`, `sshpw`). The only binary asset is the project's own
  `assets/branding/` pak.
- **Commits** follow Conventional Commits. Release-please manages versions
  and changelogs; do not edit `version.txt`, `CHANGELOG.md` or
  `.release-please-manifest.json` by hand.
- **Python** uses Poetry and existing `make` targets. Keep comments for
  non-obvious engine behavior only.
