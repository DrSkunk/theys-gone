# Co-op status and roadmap

The goal is for a second player to join the host's save and help with the
host's missions. Story progress stays on the host's save; this is not a fully
shared campaign or a thin client. This is the current status for the repository's
protocol v11 code. The [research log](REPLICATION-RESEARCH.md) contains the
evidence; its older plans are not the current roadmap.

**Evidence matters:** two games on one PC are a paired lab test, not a
two-PC test. An implemented feature, a debug command, or a passing unit test
does not prove an entire gameplay path.

## What works, and what was tested

| Area | Evidence and limits |
|---|---|
| Sessions and players | Two-PC UDP tests covered avatars and animation, held items, motorcycles, passenger riding, appearance, friendly fire, time and weather. Steam reached shared gameplay on two PCs, but host-save transfer and newer gameplay paths over Steam remain unverified. Internet/NAT behavior and native Windows also remain unverified. |
| Host saves and story | Two PCs transferred and loaded a host save with the guest sandbox active. Forced divergence, an objective autosave, and one job's start/completion exercised catch-up. Full campaign progression and joining mid-mission need more testing. A running host story mission defers catch-up until a later host save. |
| Enemies and free roam | Two-PC tests covered host-owned enemies, guest damage and split/regroup ownership using debug-spawned enemies. `FreeRoam=1` is opt-in. The v9 guest-side host proxy has **not** been proven to make naturally spawned guest-owned enemies attack and damage the host after regrouping. |
| Mission policy | In a one-PC paired `OBR` test, a riding guest was let off and could not re-board while the story reserved the seat. In `AON1`, 9 of 13 Rippers targeted the stand-in before the stealth rule and 0 after. More missions and natural combat remain open. |
| Companions and missions | One-PC tests showed Boozer and his bike as display replicas, host mission weather, the partner map marker and partner hiding in the `WNG` flashback. Guest mission-start fade recovery is implemented; other companions, vehicles and cinematics need coverage. |
| Menu timing | One-PC tests covered weapon-wheel timing and shared menu pause. A combat tutorial paused only the host while the guest kept running. Separate-PC, focus/combat and other scripted pauses remain open. |
| Loading and travel | One-PC tests covered a native host autosave load and host death from debug-spawned enemies: the guest stayed connected and the marker returned. Fast travel left the guest 446–466 m behind; guest fast travel regrouped them, but its clock was corrected to the host's. F7 once put the guest in a ceiling. |
| Sleep | In one paired lab run, host sleep advanced the separated guest's clock to morning. Two guest sleep attempts did not complete while connected; the same guest slept after disconnect. The cause and two-PC behavior remain unverified. |

Important limits for players:

- The guest save guard can fail. The F8 menu reports failure, but joining is
  not blocked and the real saves may then be written. Back up saves and verify
  the guard activated. After a successful join, the sandbox remains active
  after disconnect until the game restarts. A cached transfer snapshot may
  also lag a recent host save.
- Missions, inventory, pickups and doors are not shared live. Optional
  completed-objective sync does not replace host-save catch-up.
- Enemy and companion pawns share a cap of 16 streamed candidates per sender,
  normally within 80 m; a retained fight can extend beyond that radius. Hordes
  cannot be represented completely. Melee damage is estimated and ranged
  attacks on the guest are unverified.
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
   The existing F8 status alone does not block unprotected play.
   *Start:* `src/coop/save_guard.cpp` (`SaveGuardActive`),
   `src/coop/save_transfer.cpp` (the `Save sandbox failed` status) and the
   `InstallSaveGuard` call in `coop.cpp` startup.
   *Done when:* with the guard forced to fail, the guest cannot load the host's
   save or enter shared gameplay and sees a clear warning; the rule has a unit
   test, and a lab run records both the forced-failure and normal paths.
2. **Make joining reliable on both transports.** Verify host-save transfer and
   invite acceptance over Steam. Test joining mid-mission, joining right after
   a host save (the cached snapshot can lag) and rejoining after a disconnect.
   *Start:* `src/coop/steam.cpp`, `session.hpp`, `save_transfer.cpp` and
   `savesync.hpp`.
   *Done when:* each case has a PASS/FAIL entry in the research log, with
   Steam cases run on two PCs; failures become their own roadmap items.
3. **Follow the host through the campaign.** Play host story missions with a
   connected guest and confirm catch-up after each host save, including
   mission-start fades, deaths and checkpoint reloads. Use legitimately
   reached `RNA1`/`LRH1` saves to check region changes, guest resync and
   replica rebuilding.
   *Start:* `src/coop/story.hpp` (`StoryNeedsResync`, `StoryResyncAllowed`),
   the story resync logic in `coop.cpp` and `save_policy.hpp`.
   *Done when:* a run of consecutive host story missions with a connected guest
   is logged, with the guest on the host's progress after each host save,
   including one region change.
4. **Broaden mission policy coverage.** Test the `RBC` Lisa seat, an already
   fighting enemy in stealth, additional cutscenes, and the partner's return
   after flashbacks. `OBR`, `AON1` and `WNG` do not cover every mission.
5. **Make the guest's combat count in missions.** Test real aimed guest shots
   and melee on mission enemies, ranged enemy attacks on the guest, stand-in
   collision and possible duplicate damage. Archive both logs.
6. **Broaden companion and vehicle replicas.** Test more mission companions,
   their meshes and materials, mission trucks, and moving vehicles. Decide
   whether any interactions should work on these currently display-only
   replicas.
7. **Keep the players together.** Decide whether fast travel should offer a
   regroup; prevent teleport placement inside nearby geometry. Reproduce the
   connected guest's failed sleep and choose a shared sleep policy. Decide how
   tutorial pauses should behave while the other player is active.
8. **Plan for story hordes.** Measure a real horde mission, frame time,
   bandwidth and the selected pawns with both players; the debug spawner stops
   at 16. Choose a representation or ownership policy for
   [hordes of 25–500](https://daysgone.fandom.com/wiki/Hordes).
9. **Broaden menu timing QA.** Cover controller/mouse menus over Steam and
   repeat menu timing on separate PCs, including joins/disconnects with a
   wheel open, focus/combat overlap, photo mode and scripted/cinematic pause
   transitions. Monitor for new frame-time spikes.
10. **Track game updates, branches and DLC.** The mod supports Steam build
    19221447 only; fixed RVAs must be re-derived after an update. Document
    recovery when Steam offers no rollback to that build; `Classic` is a
    different branch. Test the unverified Broken Road modes and saves or assets
    shared between owners and non-owners. Replicas requiring assets missing on
    a peer are skipped.

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

### Housekeeping

- **Polish the partner map icon.** Choose a distinguishable texture and
  verify minimap and world-map placement and readability.
- **Explain phantom disconnect messages.** During Deck testing, the native
  CO-OP history showed disconnect/leave messages with no matching transport
  log entries while the UDP session stayed connected. Until the cause is
  found, do not treat those messages alone as proof of a disconnect.

The separate [native replication handoff](REPLICATION-HANDOFF.md) is research,
not a prerequisite for this roadmap. Use the [isolated lab](../tools/lab/README.md)
and record paired results in the research log.
