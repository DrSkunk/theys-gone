# Co-op status and roadmap

The goal is for a second player to join the host's save and help with the
host's missions. Story progress stays on the host's save; this is not a fully
shared campaign or a thin client. This is the current status for the repository's
protocol v13 code. The [research log](REPLICATION-RESEARCH.md) contains the
evidence; its older plans are not the current roadmap.

**Evidence matters:** two games on one PC are a paired lab test, not a
two-PC test. An implemented feature, a debug command, or a passing unit test
does not prove an entire gameplay path.

## What works, and what was tested

QA levels are defined in
[DEVELOPMENT.md](DEVELOPMENT.md#qa-levels-and-evidence). No area is
`automated` yet: lab runs are scripted but scored by reading logs.

| Area | QA level | Evidence and limits |
|---|---|---|
| Sessions and players | two-PC | Two-PC UDP tests covered avatars and animation, held items, motorcycles, passenger riding, appearance, friendly fire, time and weather. On 2026-11-06 a no-INI two-PC Steam run covered JOIN A FRIEND, host-save transfer (7.5 MB in about 36 s, with Steam send-limit failures), avatars, PLAYER SETTINGS with a gamepad, teleport and passenger riding; the riding has known defects (item 4). Internet/NAT behavior and native Windows also remain unverified. |
| Host saves and story | two-PC (save transfer; catch-up of a debug-driven job start and completion) | Two PCs transferred and loaded a host save with the guest sandbox active. Forced divergence, an objective autosave, and one job's start/completion exercised catch-up. Full campaign progression and joining mid-mission need more testing. A running host story mission defers catch-up until a later host save. |
| Enemies and free roam | two-PC (debug-spawned) | **Broken for natural enemies:** in the 2026-11-06 two-PC Steam run the host streamed no enemies at all while a swarm attacked; the guest had despawned its own AI and took damage from enemies it could not see (item 2). Earlier two-PC tests covered host-owned enemies, guest damage and split/regroup ownership using debug-spawned enemies. `FreeRoam=1` is opt-in and decided by the host. The v9 guest-side host proxy has **not** been proven to make naturally spawned guest-owned enemies attack and damage the host after regrouping. |
| Mission policy | paired-lab | In a one-PC paired `OBR` test, a riding guest was let off and could not re-board while the story reserved the seat. In `AON1`, 9 of 13 Rippers targeted the stand-in before the stealth rule and 0 after. More missions and natural combat remain open. |
| Companions and missions | paired-lab | One-PC tests showed Boozer and his bike as display replicas, host mission weather, the partner map marker and partner hiding in the `WNG` flashback. Guest mission-start fade recovery is implemented; other companions, vehicles and cinematics need coverage. |
| Menu timing | paired-lab | One-PC tests covered weapon-wheel timing and shared menu pause. A combat tutorial paused only the host while the guest kept running, and again on two PCs ("Fight or flight", 2026-11-06; item 3). Separate-PC, focus/combat and other scripted pauses remain open. |
| Loading and travel | paired-lab | One-PC tests covered a native host autosave load and host death from debug-spawned enemies: the guest stayed connected and the marker returned. Fast travel left the guest 446–466 m behind; guest fast travel regrouped them, but its clock was corrected to the host's. F7 once put the guest in a ceiling. |
| Sleep | paired-lab | In one paired lab run, host sleep advanced the separated guest's clock to morning. Two guest sleep attempts did not complete while connected; the same guest slept after disconnect. The cause and two-PC behavior remain unverified. |

Important limits for players:

- A guest whose save sandbox cannot start leaves the session (one-PC lab run),
  but the sandbox only covers the game executable's own file calls. Back up saves. After a successful join, the
  sandbox remains active after disconnect until the game restarts. A cached
  transfer snapshot may also lag a recent host save.
- Missions, inventory, pickups and doors are not shared live.
  Completed-objective sync is on by default but experimental (tested on one
  job) and does not replace host-save catch-up. The host decides `HostSave`,
  `MissionObjectiveSync` and `FreeRoam`; the guest follows automatically.
- Natural enemies did not reach the guest at all in the latest two-PC run,
  and the guest can be hurt by enemies it does not see (item 2). The stream
  is also capped at 16 enemy and companion pawns within about 80 m, so hordes
  cannot be represented yet. Melee damage is estimated and ranged
  attacks on the guest are unverified.
- Player settings moved from the removed F8 overlay to the native CO-OP →
  **PLAYER SETTINGS** page. Lab runs covered the page in both menus, and a
  two-PC run covered it with a Steam Deck gamepad.
- Teleporting beside a partner near furniture can place a player inside
  geometry. Guest sleep and tutorial pauses can also behave differently from
  their host-side equivalents.
- LAN UDP is unauthenticated: use a trusted network and never expose its port
  to the Internet. Steam transport is authenticated and encrypted by Steam.

## Next, in priority order

Work is ordered by the core use case: a guest joins the host's save and helps
with the host's missions.

1. **Make joining the host's save safe.** Prevent guest play from silently
   continuing without an active save guard; make failure and recovery
   prominent, and notify guests that the sandbox persists after disconnect.
   The CO-OP status text alone does not block unprotected play.
   *Start:* `src/coop/save_guard.cpp` (`SaveGuardActive`),
   `src/coop/save_transfer.cpp` (the `Save sandbox failed` status) and the
   `InstallSaveGuard` call in `coop.cpp` startup.
   *Done when:* with the guard forced to fail, the guest cannot load the host's
   save or enter shared gameplay and sees a clear warning; the rule has a unit
   test, and a lab run records both the forced-failure and normal paths.
   *Status:* both paths passed a one-PC lab run on 2026-11-06, and the
   "sandbox stays until restart" notice on leaving passed a second run that
   day (research log). Remaining: at the title screen the block shows only in the CO-OP lobby log
   and status, because the HUD notice is unavailable there; show it in the
   title menu itself.
2. **Show every host enemy on the guest.** The 16-enemy cap is not
   acceptable: a seamless session must show whole swarms and hordes. In the
   2026-11-06 two-PC run the host streamed no enemies while a swarm attacked
   the guest's stand-in, and the guest took that damage from enemies it could
   not see. The scan skipped every pawn without an AI controller as pooled,
   which also drops horde members promoted to full pawns (they are driven by
   a `BendCrowdController` actor); with `[Research] PawnEnemies=1` pools are
   recognised by their hidden and pool flags instead, but no natural swarm
   has been tested yet
   ([research log](REPLICATION-RESEARCH.md#enemy-scan-pools-hordes-and-the-2026-11-06-gap-2026-11-07-one-pc-one-and-two-games)). Then
   replace full-skeleton streaming for most enemies with native puppets: the
   guest spawns the same class with its AI stopped and drives its position,
   speed and facing from a compact state record (about 16–20 bytes), so the
   game animates it; full poses only for the few enemies near a player in an
   attack, stagger or death. Never forward a stand-in hit from an enemy the
   guest does not show.
   *Status:* puppets exist behind `[Research] PawnEnemies=1` (protocol v13,
   `src/coop/standin.cpp`): about 112 bytes per enemy at 15 Hz plus
   class, one-shot and death events. In the paired lab a host swarmer's
   attacks, grapples, the guest's hits and the host's kill all showed
   through the stand-in. With 8 swarmers, enemy traffic fell from 103 to
   about 17 kB/s once the guest acknowledged its stand-ins. A dead stand-in
   hands over to the pose replica of the owner's corpse, which avoids the
   guest's kill reward. The one-PC probe work behind it is in the research
   log from
   [native-style enemy animation](REPLICATION-RESEARCH.md#pawn-mirror-native-style-enemy-animation-from-movement-and-state-2026-11-06-one-pc-one-game)
   to the
   [pack run](REPLICATION-RESEARCH.md#pawn-stand-ins-dropping-poses-and-a-pack-of-eight-2026-11-07-one-pc-two-games).
   A two-PC Steam run (desktop host, Steam Deck guest) repeated it: 8
   stand-ins, 16–17 kB/s, hits and death forwarded; 51 of 129 one-shots
   were missed on the Deck
   ([research log](REPLICATION-RESEARCH.md#pawn-stand-ins-over-steam-first-two-pc-run-2026-11-07-two-pcs-steam)).
   That gap was an object lookup that could not match names ending in
   digits; fixed, the paired lab played 96 of 96 and 55 of 55
   ([research log](REPLICATION-RESEARCH.md#pawn-stand-ins-the-missing-sequences-were-a-name-lookup-bug-2026-11-07-one-pc-one-and-two-games)).
   With stand-ins the scan now keeps up to 64 enemies (poses only for the
   nearest 16); 16 stand-ins cost 28 kB/s in the lab
   ([research log](REPLICATION-RESEARCH.md#pawn-stand-ins-lifting-the-16-enemy-cap-2026-11-07-one-pc-two-games)).
   With natural enemies from `spawn-horde` the paired lab streamed up to 35
   at once (about 1.5 kB/s each); a frame hitch when many stand-ins spawn at
   once is fixed
   ([research log](REPLICATION-RESEARCH.md#pawn-stand-ins-past-16-enemies-2026-11-08-one-pc-two-games)).
   A two-PC Steam run with a Steam Deck guest showed 21 stand-ins at 31
   kB/s, hits and the kill forwarded
   ([research log](REPLICATION-RESEARCH.md#pawn-stand-ins-over-steam-past-16-enemies-2026-11-08-two-pcs-steam)).
   A group of new stand-ins no longer costs an object scan
   ([research log](REPLICATION-RESEARCH.md#pawn-stand-ins-finding-a-stand-ins-weapons-without-an-object-scan-2026-11-08-one-pc-two-games)).
   Next: a real horde (none could be spawned or woken yet) and the scan gap
   above.
   *Start:* `FinishEnemyScan` and `SharedEnemy` in `coop.cpp`,
   `src/coop/enemy.hpp` (`kEnemyMaxStreamed`, packets), `BendCrowdGroupHorde`
   and `HordeSwarmerPawn_C` in the reflection dump.
   *Done when:* in a lab run near natural swarmers and in one two-PC run, every
   host enemy within range appears on the guest, a 100+ horde stays under the
   Steam send budget without new frame spikes, and no guest damage comes from
   an enemy it does not show.
3. **Pause both games for tutorial popups.** A game popup that pauses one
   player (such as the "Fight or flight" tutorial) shows on both PCs at once,
   and play resumes only once both players have dismissed it.
   *Start:* the tutorial widget and the function that opens it (reflection
   dump), the menu-pause flag (`kMenuPause`) and `UpdateMenuPause` in
   `coop.cpp`.
   *Done when:* a two-PC run shows a tutorial on both PCs, both worlds stay
   paused until the second player dismisses it, and either player's dismissal
   order works.
4. **Ride on the native passenger seat.** Riding along hides the guest's own
   character and poses a stand-in, so on two PCs the passenger could still
   attack, held a misplaced weapon, saw no **Get off** prompt, had no mount
   animation, and the **Ride with** label was cropped. Spawn a real bike
   actor on the guest that follows the host's stream with physics off and
   seat the guest with the game's own `TryEnterVehicle` / `TryExitVehicle`
   and passenger camera (`BendVehiclePassengerCamFollow`). If that fails,
   block attacks and holster the weapon while seated, show the prompt and fix
   the label size.
   *Start:* the passenger code and `ride_prompt.cpp` in `src/coop/`, seat
   functions in the reflection dump.
   *Done when:* a lab run and a two-PC run show the guest seated with native
   animation, no attack input, a visible **Get off** prompt and an uncropped
   label.
5. **Score lab runs automatically.** Add a lab scenario runner that drives
   both instances, asserts the mode first (both peers connected, `HostSave`
   sandbox active), checks shared state and prints `RESULT: PASS`, `FAIL` or
   `SKIP`; a skip or a check that never ran is not a pass. Let allowlisted debug
   commands write a machine-readable reply (status, story signature, save-guard
   state) so checks do not depend on log wording. Make `lab.sh` hash the base
   copy's saves before and after each run and fail on any change.
   *Start:* `tools/lab/lab.sh`, `tools/debug_control.py`, the debug command
   handler in `src/coop/coop.cpp`; method in coop-anything's
   [automated QA](https://github.com/cranebeaker/coop-anything/blob/main/playbook/automated-qa.md).
   *Done when:* scenarios for a normal join and a forced save-guard failure run
   unattended, the scorer has its own tests in `make test`, and both results are
   logged. Areas proven this way may then be marked `automated`.
6. **Make joining reliable on both transports.** Verify host-save transfer and
   invite acceptance over Steam. Test joining mid-mission, joining right after
   a host save (the cached snapshot can lag) and rejoining after a disconnect.
   Repeat the save transfer over a lossy, partitioned link between lab instances
   (`tc netem`, or coop-anything's chaos relay). Over Steam the guest pulls 24
   chunks of 1 KB per tick, more than Steam's send rate accepts: a 7.5 MB
   transfer logged about 1,300 `send failed (result 25)` lines and took 36 s.
   Pace the pulls to the transport, or raise Steam's send rate through a
   vtable slot derived from the binary.
   *Start:* `src/coop/steam.cpp`, `session.hpp`, `save_transfer.cpp` and
   `savesync.hpp`.
   *Done when:* each case has a PASS/FAIL entry in the research log, scored by
   the lab runner where the lab can cover it and with Steam cases run on two PCs; failures become their own roadmap items.
7. **Follow the host through the campaign.** Play host story missions with a
   connected guest and confirm catch-up after each host save, including
   mission-start fades, deaths and checkpoint reloads. Use legitimately
   reached `RNA1`/`LRH1` saves to check region changes, guest resync and
   replica rebuilding.
   *Start:* `src/coop/story.hpp` (`StoryNeedsResync`, `StoryResyncAllowed`),
   the story resync logic in `coop.cpp` and `save_policy.hpp`.
   *Done when:* a run of consecutive host story missions with a connected guest
   is logged, with the guest on the host's progress after each host save,
   including one region change.
8. **Prove the rule tests can fail.** Add mutation twins for the save, story
   and mission-policy rules: a test-only switch disables each rule and the
   matching test must then fail.
   *Start:* `src/coop/save_policy.hpp`, `story.hpp`, `mission_policy.hpp`,
   `tools/test_protocol.cpp`; method in coop-anything's automated QA page.
   *Done when:* a `make` target runs every twin and fails if any twin passes.
9. **Broaden mission policy coverage.** Test the `RBC` Lisa seat, an already
   fighting enemy in stealth, additional cutscenes, and the partner's return
   after flashbacks. `OBR`, `AON1` and `WNG` do not cover every mission.
10. **Make the guest's combat count in missions.** Test real aimed guest shots
   and melee on mission enemies, ranged enemy attacks on the guest, stand-in
   collision and possible duplicate damage. Archive both logs.
11. **Broaden companion and vehicle replicas.** Test more mission companions,
   their meshes and materials, mission trucks, and moving vehicles. Decide
   whether any interactions should work on these currently display-only
   replicas.
12. **Keep the players together.** Decide whether fast travel should offer a
   regroup; prevent teleport placement inside nearby geometry. Reproduce the
   connected guest's failed sleep and choose a shared sleep policy.
13. **Take story hordes through missions.** After item 2, play a horde
    mission with both players and measure frame time, bandwidth and ownership
    for [hordes of 25–500](https://daysgone.fandom.com/wiki/Hordes); the debug
    spawner stops at 16.
14. **Broaden menu timing QA.** Cover controller/mouse menus over Steam and
    repeat menu timing on separate PCs, including joins/disconnects with a
    wheel open, focus/combat overlap, photo mode and scripted/cinematic pause
    transitions. Monitor for new frame-time spikes.
15. **Track game updates, branches and DLC.** The mod supports Steam build
    19221447 only; fixed RVAs must be re-derived after an update. Document
    recovery when Steam offers no rollback to that build; `Classic` is a
    different branch. Test the unverified Broken Road modes and saves or assets
    shared between owners and non-owners. Replicas requiring assets missing on
    a peer are skipped.

### Manual checks

The lab sends keyboard input to one PC and scores runs by reading logs, so
these checks need a person, a controller or a second PC. Use an isolated lab
copy or two-PC setup, never a real install or save. Record each result in the
research log and remove it here once it is covered.

Combat and enemy stand-ins (items 2 and 10):

- **Counter a swarmer with a controller.** Press Parry (Y / Triangle) in the
  counter window while `anim-mirror-pawn` logs, with and without
  `mute-source-counter`. Note what happens to the attacker (stagger,
  knockdown, damage, kill) and whether `OnCounterAttackSuccessful` fires.
  The lab showed keyboard and mouse have no counter: the left click is a
  plain melee swing.
- **Counter prompt on the guest.** On a guest screen, does the stand-in's
  `CounterIcon` draw a prompt, and only for attacks aimed at the guest?
- **Stand-in look.** Watch a stand-in attack, grapple and move beside its
  source: foot sliding, snapping between states, wrong facing or offset.
- **Enemy death on the guest.** Does a killed enemy fall and lie as a corpse
  on the guest, or freeze or vanish?
- **Ranged attacks on the guest.** Shots from human enemies at the guest:
  do they hit and do they hurt?

Menus and settings:

- **Gamepad navigation** of the CO-OP lobby and PLAYER SETTINGS pages, and the
  View+X shortcut.
- **Title-menu PLAYER SETTINGS**, the guest-side page, F9, and the "sandbox
  stays active" notice on leaving.
- **Menu timing on separate PCs** with a controller, a wheel open during
  join or disconnect, photo mode and cinematic pauses (item 14).

Sessions, saves and travel (two PCs):

- **Steam transport** with host-save transfer and the newer gameplay paths;
  Internet/NAT play; a native Windows host or guest.
- **Guest sleep while connected**, which did not complete in the lab.
- **Fast travel**: the guest landed 446–466 m behind; check regrouping and
  the clock on two PCs.
- **Joining mid-mission** and a longer stretch of campaign progress.
- **Broken Road modes** and DLC assets shared between owners and non-owners
  (item 15).

### Later: separated play

These matter only when players split up with `FreeRoam=1`, outside the core
use case.

- **Validate the guest-side host proxy.** Split up with naturally spawned
  guest-owned enemies; confirm the host receives their replicas before
  regrouping, then show those enemies targeting the host and reducing host
  health. Keep this experimental if inconclusive.
- **Share world authority while separated.** Define conflict handling for
  interactions, pickups and mission progress in the guest's region; test
  simultaneous interaction and subsequent save/resync.
- **Preserve enemy identity across ownership release.** Test split, regroup,
  death, unload and reload without duplicates or forgotten enemies.
  Give each ownership assignment an epoch so stale or echoed handoffs are
  rejected, as in coop-anything's
  [actor ownership](https://github.com/cranebeaker/coop-anything/blob/main/playbook/actor-ownership.md) page.

### Housekeeping

- **List the mod in the coop-anything directory.** Once an area is
  `automated`, propose a Days Gone entry and write-up to
  [coop-anything](https://github.com/cranebeaker/coop-anything) with an honest
  QA level, after the owner reviews the draft.

- **Polish the partner map icon.** Choose a distinguishable texture and
  verify minimap and world-map placement and readability.
- **Explain phantom disconnect messages.** During Deck testing, the native
  CO-OP history showed disconnect/leave messages with no matching transport
  log entries while the UDP session stayed connected. Until the cause is
  found, do not treat those messages alone as proof of a disconnect.

The separate [native replication handoff](REPLICATION-HANDOFF.md) is research,
not a prerequisite for this roadmap. Use the [isolated lab](../tools/lab/README.md)
and record paired results in the research log.
