# Co-op status and roadmap

The goal is reliable two-player co-op in the host's world, not a fully shared
campaign or a thin client. This is the current status for the repository's
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
| Menu timing | One-PC paired tests covered weapon-wheel timing and shared pause: both pause menus freeze gameplay, either closing resumes it, and disconnect restores native pausing. Separate-PC, focus/combat and cinematic cases remain open. |
| Loading and travel | A one-PC native load of an older host autosave kept the session connected, rebuilt the marker and synced the guest clock. In one fast-travel test the guest stayed about 466 m behind despite receiving the host's position and time. Host death and bedroll sleep were not tested. |

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
- LAN UDP is unauthenticated: use a trusted network and never expose its port
  to the Internet. Steam transport is authenticated and encrypted by Steam.

## Next, in priority order

1. **Protect guest saves on failure.** Prevent guest play from silently
   continuing without an active save guard; make failure and recovery
   prominent, and notify guests that the sandbox persists after disconnect.
   The existing F8 status alone does not block unprotected play.
2. **Validate combat and the guest-side host proxy.** Split up with naturally
   spawned guest-owned enemies; confirm the host receives their replicas before
   regrouping, then show those enemies targeting the host and reducing host
   health. Archive both logs; keep this experimental if inconclusive. Also
   test real aimed guest shots and melee, ranged enemy attacks, stand-in
   collision and possible duplicate damage.
3. **Test death, sleep and real region transitions.** Exercise host death,
   bedroll sleep, repeated loads, recent-save transfers and a different mission
   state. Use legitimately reached `RNA1`/`LRH1` saves to check region changes,
   guest resync and replica rebuilding. Decide whether fast travel should offer
   a regroup, and verify free-roam ownership at the resulting separation.
4. **Broaden mission policy coverage.** Test the `RBC` Lisa seat, an already
   fighting enemy in stealth, additional cutscenes, and the partner's return
   after flashbacks. `OBR`, `AON1` and `WNG` do not cover every mission.
5. **Broaden companion and vehicle replicas.** Test more companions, their
   meshes and materials, mission trucks, and moving vehicles. Decide whether
   any interactions should work on these currently display-only replicas.
6. **Plan for hordes.** Measure a real horde, frame time, bandwidth and the
   selected pawns with both players; the debug spawner stops at 16. Choose a
   representation or ownership policy for
   [hordes of 25–500](https://daysgone.fandom.com/wiki/Hordes).
7. **Share world authority while separated.** Define and implement conflict
   handling for interactions, pickups and mission progress in the guest's
   region; test simultaneous interaction and subsequent save/resync.
8. **Preserve enemy identity across ownership release.** Test split, regroup,
   death, unload and reload without duplicates or forgotten enemies.
9. **Broaden Steam and menu timing QA.** Verify host-save transfer, invite
   acceptance and controller/mouse menu coverage over Steam. Repeat menu timing
   on separate PCs, including joins/disconnects with a wheel open,
   focus/combat overlap, photo mode and scripted/cinematic pause transitions.
   Monitor for new frame-time spikes.
10. **Polish the partner map icon.** Choose a distinguishable texture and
    verify minimap and world-map placement and readability.
11. **Track game updates and branches.** The mod supports Steam build 19221447
    only; fixed RVAs must be re-derived after an update. Document recovery when
    Steam offers no rollback to that build; `Classic` is a different branch.
12. **Test the Broken Road DLC modes.** Horde Assault, Permadeath, Speedrun,
    and saves or assets shared between owners and non-owners are untested.
    Replicas requiring assets missing on a peer are skipped.
13. **Retire or harden the legacy `tools/same-pc` launchers.** They default to
    the real Steam installation and do not select UDP. Use the isolated lab
    and `fake_peer.py` instead.

The separate [native replication handoff](REPLICATION-HANDOFF.md) is research,
not a prerequisite for this roadmap. Use the [isolated lab](../tools/lab/README.md)
and record paired results in the research log.
