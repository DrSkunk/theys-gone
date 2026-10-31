# Co-op roadmap

The goal is reliable, shared story co-op—not full engine replication. The host
owns the save and story; the joining player helps in the host's world.

Status means:

- `[x]` demonstrated in a paired lab session; the scope of the test is stated.
- `[~]` implemented or partly tested, but required validation or coverage is
  still missing.
- `[ ]` not implemented or not yet tested.

Paired lab tests may run two game instances on one PC. They are useful evidence,
but do not replace a two-PC test when a task specifically needs one. See the
[lab guide](../tools/lab/README.md) and
[research log](REPLICATION-RESEARCH.md) for test setup and evidence.

## Done

- [x] Headless two-game lab on one PC.
- [x] Lift the joining player's stuck mission-start fade and move them next to
  the host.
- [x] Determine the local mission from its running script, not stale
  `ActiveMissionData`.
- [x] Show the partner on the minimap and world map.
- [x] Reduce frame-time peaks caused by `VirtualQuery` in object scans.
- [x] Cap repeated in-place reloads of an adopted host save.
- [x] Send the host's running mission in protocol v10 and hide the partner
  during flashback missions. Tested with `WNG`; broader cinematic coverage
  remains open below.
- [x] Apply the host's mission weather on the joining player's game. Matched
  fog was observed in `OBR`.
- [x] Stream Boozer as a friendly, textured replica and show Boozer's bike in
  the `OBR` one-PC paired lab. Other companions and vehicles are not covered
  by this result.
- [x] Capture-mission policy decision: do not restrict either player; keep the
  shared escape as the current behavior.
- [x] One-PC paired menu timing: both real games' weapon wheels run at normal
  speed; Escape/map/inventory pause both worlds only while both players have
  a menu open. Either close resumes; disconnect restores native pausing.
  Both playthrough clocks froze and networking continued during shared pause.

## Still to do

1. [~] **Validate story passenger-seat and stealth policies in-game.** The
   mission-policy code is implemented, but the paired in-game tests are not
   complete.
   - In `RBC`, test the guest riding before Lisa boards; confirm the story
     passenger seat is reserved and the guest can safely dismount.
   - In a stealth mission, confirm enemies already fighting can target the
     partner stand-in, while enemies not already fighting are not alerted.
   - Record both outcomes in the research log before marking this complete.

2. [ ] **Validate the guest-side host proxy after regrouping.** Previous tests
   used debug-spawned enemies and did not exercise the required path.
   - Use naturally spawned, guest-owned enemies while the players are apart.
   - Confirm the host replica is present before regrouping.
   - Regroup; confirm those enemies target the host and the host loses health.
   - Archive both game logs and record the result. Keep the feature experimental
     if the test remains inconclusive.

3. [~] **Validate real region transitions.** Far-teleport and save-resync
   handling is implemented, but a valid `RNA1`/`LRH1` transition has not been
   demonstrated.
   - Use a save that has legitimately reached the relevant region.
   - Have the host cross the boundary and save; verify the guest loads into the
     correct region and appears near the host.
   - Record any locked-region or resync behavior that prevents the test.

4. [~] **Broaden companion and mission-vehicle replicas.** Boozer and his bike
   are the only demonstrated companion cases.
   - Test other companions and their meshes/materials.
   - Test mission vehicles such as trucks, plus replica appearance and
     animation while moving.
   - Decide whether interactions such as the ride prompt should work on
     display-only replicas; document the intended behavior.

5. [ ] **Share world authority when players are apart.**
   - Define host-authoritative behavior for interactions, pickups, and mission
     progress in the guest's region.
   - Implement synchronization and conflict handling.
   - Test both players acting on the same object and verify save/resync results.

6. [ ] **Preserve enemy identity across ownership changes.**
   - Carry a stable enemy identity through ownership release and reacquisition.
   - Test split, regroup, death, unload, and reload without duplicate or
     forgotten enemies.

7. [ ] **Polish the partner map icon.**
   - Choose a marker texture that is distinguishable from the player's icon.
   - Verify placement and readability on both minimap and world map.

8. [~] **Check partner visibility through additional cinematics.** Flashback
   hiding is implemented and `WNG` was tested; the `OBR` intro was also
   observed. Test additional cutscenes and confirm the partner returns when a
   flashback ends.

9. [~] **Broaden menu timing QA.** Wheel opening/closing and shared pause
   passed the one-PC paired lab. A simulated-peer test also covered joining
   with a wheel already open and preservation of another slowdown.
   - Repeat with separate PCs and Steam, including joins/disconnects while a
     wheel is open and actual focus/combat overlap.
   - Cover photo mode and scripted/cinematic pause transitions without
     accidentally sharing or cancelling their pauses.
   - Investigate focus loss, other scripted menus and cinematic timing before
     changing them; these paths remain unchanged.

## Out of scope for this roadmap

Full Unreal engine replication with the guest as a thin client remains a
separate research effort. Steam sessions between two PCs have been tested;
the one-PC lab uses UDP because its sandboxed Steam client is not reachable
from the mod.
