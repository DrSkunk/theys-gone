# AGENTS.md

Theys Gone is a two-player co-op ASI plugin for Days Gone PC, Steam build
19221447 (UE4 BendGame 4.11). Read the relevant guide before changing code.

**Focus:** a guest joins the host's save and helps with the host's missions;
story progress stays on the host's save. Prioritize save safety, joining and
mission co-op; separated free-roam play is later work (see the roadmap).

| Need | Start here |
|---|---|
| Current status and prioritized next work | [Co-op roadmap](docs/COOP-ROADMAP.md) |
| How to work on and test a roadmap item | [Working on a roadmap item](docs/DEVELOPMENT.md#working-on-a-roadmap-item) |
| Player setup and limitations | [README](README.md) |
| Design, packets and engine behavior | [How it works](docs/HOW-IT-WORKS.md) |
| Build, debug tools and releases | [Development](docs/DEVELOPMENT.md) |
| Live-test evidence | [Research log](docs/REPLICATION-RESEARCH.md) |
| Isolated two-game lab | [Lab guide](tools/lab/README.md) |
| Two-PC test sessions | [Two-PC tooling](tools/twopc/README.md) |
| Mission catalog and policies | [Story co-op](docs/STORY-COOP.md) |
| Native-replication research | [Research handoff](docs/REPLICATION-HANDOFF.md) |

Most gameplay logic is in `src/coop/coop.cpp`. Pure rules live in headers
such as `enemy.hpp`, `protocol.hpp`, `streaming.hpp`, `story.hpp`,
`mission_policy.hpp` and `save_policy.hpp`, with tests in `tools/test_protocol.cpp`. Engine replication
experiments belong in `src/coop/research.cpp` behind `[Research]` INI keys;
they are not normal play.

## Commands

```sh
poetry install   # Python tooling; never create your own venv
make test        # C++ protocol/motion/combat and Python unit tests
make build       # package/TheysGone.asi and branding pak
make dist        # versioned zip and SHA-256
git diff --check
```

The build requires MinGW-w64 (`x86_64-w64-mingw32-g++`, GCC 13+, POSIX
threads); see [prerequisites](docs/DEVELOPMENT.md#prerequisites). Run
`make test build` before every push; CI runs the same targets.

## Rules

- **Evidence over claims.** Record each live test, its scope and log evidence
  in the research log; update the roadmap when status changes. One-PC paired
  tests and unit tests do not establish two-PC gameplay. Separate observed,
  derived and unknown, mark each area's
  [QA level](docs/DEVELOPMENT.md#qa-levels-and-evidence), and stop to correct
  any claim the evidence contradicts.
- **Scripts before playtests.** Prefer a repeatable lab scenario with a
  PASS/FAIL result; a SKIP or a check that never ran is not a pass.
- **Build-specific offsets.** Addresses and offsets target build 19221447
  only. Derive them with reflection or `tools/re/`; never guess.
- **Protocol changes.** Changing a packet layout or meaning bumps
  `kProtocolVersion` in `src/coop/protocol.hpp` (or the datagram's own
  version), updates `tools/test_protocol.cpp` and `VERSION` in
  `tools/fake_peer.py`, and uses a breaking commit (`feat!:`).
- **Live testing.** Never touch a player's game install or saves. Test in
  isolated copies (the [lab](tools/lab/README.md)) or on a dedicated test
  PC's own install, the latter only through
  [`tools/twopc`](tools/twopc/README.md) `prep` and `restore`: back up ASI,
  INI and saves first, archive logs outside the repo, then restore and verify
  hashes. Quit the game through its menu, never by killing it. Keep player,
  account and host names out of committed docs. Debug commands require
  `DebugTools=1`; see [debug tools](docs/DEVELOPMENT.md#debug-tools).
- **Never commit** game files, saves, logs, credentials or local token files
  (`apitoken`, `sshpw`). The only binary asset is the project's own
  `assets/branding/` pak.
- **Commits** follow Conventional Commits. Release-please manages versions
  and changelogs; do not edit `version.txt`, `CHANGELOG.md` or
  `.release-please-manifest.json` by hand.
- **Python** uses Poetry and existing `make` targets. Keep comments for
  non-obvious engine behavior only.
