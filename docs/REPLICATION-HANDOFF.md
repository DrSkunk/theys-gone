# Native engine replication: research handoff

The shipped mod uses its own replication; a guest as an Unreal thin client
is a separate, unsafe experiment in `src/coop/research.cpp`, behind
`[Research]` options. It is not required for the
[co-op roadmap](COOP-ROADMAP.md). Complete run details, logs and earlier
investigations are in the [research log](REPLICATION-RESEARCH.md).

## What the experiments established

- The game's default socket subsystem uses SteamSockets: `SteamNetDriver`
  worked, while `IpNetDriver` failed to bind. The mod's Steam transport must
  not drain packets while the engine owns P2P.
- Loading a new map after startup crashed in Bend code. The experiment
  substituted `MovePendingLevel`, retained the local Bend controller and used
  spectator joining before spawning a compatible native pawn.
- That transition cleared the guest's navigation system. The gated
  `ClientNavigation=1` experiment recreated it and avoided the observed
  crash; this is not proof of a generally safe transition.
- With additional experimental pawn replication, possession, movement and
  input changes, one paired run demonstrated mouse look and movement.
  Gamepad input and player-0 side effects were not established.
- A host enemy's movement replicated, but health did not until the
  experimental health lifetime property was added. A later death replicated
  health and tear-off but not the enemy's appearance or death presentation.
  A separate AI/respawn probe crashed the host; its exact trigger was not
  isolated.

These are narrow research results, not working campaign co-op. Guest world
authority, interaction, UI and mission behavior remain unresolved. Do not
repeat incompatible controller/pawn combinations or treat a successful debug
command as proof of player control.

## Code and tools

- `src/coop/research.cpp`: network probes, engine-driver experiments and
  guarded replication hooks. Fixed RVAs apply only to build 19221447.
- `src/coop/steam.cpp`: Steam P2P handling when the engine owns the session.
- `src/coop/coop.cpp`: debug command dispatch and local-controller selection.
- `tools/re/`: read-only executable, dump, cross-reference and pak-index
  analysis helpers. See [offline executable analysis](DEVELOPMENT.md#offline-executable-analysis)
  and the research log for the build-specific findings.

## Remote input and screenshots

Use the [isolated lab](../tools/lab/README.md) for paired game instances and
the [remote game-session controller](DEVELOPMENT.md#remote-game-session-controller)
for its input and screenshot interface. Never deploy experimental builds to
real player installations or saves. Back up test ASI, INI and saves, archive
logs outside the repository, and restore and verify hashes after each run.

## If resuming this research

Start from the run evidence, not old commands or machine state. Isolate one
failure or subsystem per test, retain matching replication layouts on both
peers, and validate movement, gameplay and teardown independently. Prioritize
the production co-op work in the roadmap unless native replication is the
explicit objective.
