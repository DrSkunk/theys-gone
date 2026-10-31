# Co-op roadmap: what is missing

Working list of the gaps between Theys Gone today and complete two-player
campaign co-op, in the order they are being worked on. Each item says how it
will be verified; "done" means verified in a paired lab session
([tools/lab/README.md](../tools/lab/README.md)) with the evidence in
[REPLICATION-RESEARCH.md](REPLICATION-RESEARCH.md). Background on the story
side is in [STORY-COOP.md](STORY-COOP.md).

Status: `[ ]` open, `[~]` in progress, `[x]` done (verified), `[!]` blocked.

## Already done

- [x] Passenger seat reserved for story characters in pillion missions (#11).
- [x] Partner stand-in no longer alerts enemies in stealth missions (#14).
- [x] Frame-time peaks from `VirtualQuery` in object scans (#15, #17).
- [x] Endless in-place reload of an adopted host save, capped at 3 (#18).
- [x] Headless two-game lab on one PC (#12, fixes in #18).

## Open, in working order

1. [x] **Guest stuck on a black screen after the host starts a mission.**
   Loading the host's mission-start save in place marks the mission active
   on the guest but never starts it, and its start fade stays black. Fix:
   lift the fade (`CallFadeOutOfBlack`) when no mission script runs, then
   move the guest to the host. Verify: guest frames change and show the
   world after the host starts `OBR`.
2. [x] **Guest treats a mission it is not running as active.** The guest's
   `ActiveMissionData` stays set without a mission script, so the guest
   applies mission policies (passenger seat, stealth) for a mission it does
   not play. Use "mission script running" for local policy. Verify: guest
   `status` reports no mission after following the host.
3. [x] **Partner on the minimap and the world map.** Each game shows the
   other player with a `MiniMapLocationComponent` marker that follows the
   partner avatar. First find a friendly icon in use (`minimap-probe`).
   Verify: screenshots of both minimaps and maps show the partner marker at
   the right place.
4. [x] **Corrections to merged docs.** `STORY-COOP.md` and the research log
   still say the guest "runs its own copy of the mission with its own
   Boozer"; that was based on host screenshots mislabelled as the guest's.
5. [~] **Host's companions and mission vehicles on the guest.** The guest
   cannot run its own copy of a mission, so it sees the host riding with
   nobody. Stream non-hostile mission actors (Boozer, Rikki, Sarah, their
   bikes, the turret truck) as display-only replicas, like enemies. Protocol
   change. Verify: the guest sees Boozer next to the host in `OBR`.
6. [~] **Partner avatar during cutscenes and flashbacks.** Hide the partner
   while the local game plays a cinematic or a flashback mission (`WNG`,
   `HGL`, `TSR`, `IPR`, `AQW`); check what the guest sees while the host is
   in a cutscene. Verify: screenshots from both games during `OBR`'s intro.
7. [!] **Protocol v9 guest-side proxy** (AGENTS gap 1): guest-kept enemies
   targeting and damaging the host after a regroup. Now testable with two
   real games. Verify: proxy targeting on the guest and a host health drop.
8. [ ] **Pillion character joining mid-mission** (Lisa in `RBC`): the seat
   rule while the host rides with the guest before Lisa boards.
9. [ ] **Stealth: enemies already fighting** still switch to the partner;
   confirm nothing else alerts the camp (native sight of the stand-in).
10. [x] **Capture missions** (`CRW`, `TLL2`, `PBC4`/`AON1`, `AHB5`): decided
    2026-10-30: no restriction, as in most co-op games; both players stay
    free and the escape is a shared prison break. That is the current
    behaviour (no capture policy is applied).
11. [ ] **Region transitions** (`RNA1` south, `LRH1` north): the guest
    follows the host's save; verify it lands next to the host.
12. [ ] **Shared world state** (AGENTS gap 2): interactions, pickups and
    mission state in the guest's region are not host-authoritative.
13. [ ] **Enemy identity on ownership release** (AGENTS gap 3).
14. [ ] **Steam transport in the lab**: the sandboxed Steam client is not
    reachable from the mod, so lab sessions use UDP only.

## Log

- 2026-10-29: list created. Item 1 done (guest logged "lifting its black
  screen (ok)"; ten guest screenshots showed the world). Item 4 done. New
  observation for item 5: the guest also lacks the host's mission weather
  (fog at the host, sun at the guest).
- Item 2 done: during `OBR` the host's `status` reports `mission='OBR'`, the
  guest's `mission=''`.
- Item 3: a spawned `MapLocationActor` registers a `MapLocation` that the
  minimap list includes. It has no root component, so `bGetPositionFromOwner`
  left it at the world origin; with that flag off and `Position` written each
  tick, the host's world map drew the partner icon 280 m away, and the
  guest's marker sits at the host's position. The icon keeps its own colours
  (the tint is ignored). Minimap A/B at 35 m: with `PartnerMarker=1` the
  host's minimap shows an extra triangle at the partner's offset, absent with
  `PartnerMarker=0`. Item 3 done; polish left: a more distinct icon.
- Item 6: in a paired `OBR` intro (real captures of both games) the host's
  20 frames showed no stray partner; the guest saw the host's avatar. No
  change needed so far; flashbacks not yet tested.
- Item 5, weather: time now follows the host during its missions (guest
  went to 12.5 h with the host), but the mission fog comes from the
  mission's weather sublevel, not `WorldWeather` (0 at the host); syncing it
  needs the host's mission code on the guest (new item 15).
- Item 5, companions (in progress): the host now streams pawns with a
  `BendBuddyController` (Boozer: `BoozerPawn_C_0`) with a friendly flag
  (`DMEN` v2), excluded from hits, aggro and handoff. The guest requests
  missing meshes from `BendStreamingManager.RequestAsyncLoadBPCallback` and
  rebuilds the replica once they load (Boozer: head and 3 parts after 5 s).
  With the guest's camera turned to the replica (`look-companion`), the
  guest's screenshot showed a Boozer-shaped figure walking at Boozer's
  position, 12 m away, but grey and blue rather than textured. Open: his
  materials/textures, and Boozer's bike (item 16).

## Added

15. [~] **Host mission code to the guest** (protocol change): lets the guest
    load the mission's weather sublevel, hide the partner in flashbacks and
    apply capture policies.
16. [~] **Companion vehicles**: stream companion bikes (and mission trucks)
    like the partner's bike.
17. [ ] **Partner marker icon**: the tint is ignored, so pick a texture that
    differs from the player's own icon.
- Item 7 blocked on a test setup: the guest now streams a debug-spawned pack
  while separated (the first attempt's blocker), but after the regroup all 8
  kept targeting the guest even 146 m away, likely because debug-spawned
  pawns stay on the local player. Needs natural guest-owned enemies.
- Item 15 (protocol v10): each state packet carries the sender's running
  `MissionID`; the guest resolves the host's mission (`partner runs 'WNG'`).
  Used so far for item 6: both games hide the partner during flashback
  missions. Weather sublevel and capture policies still open.
- Fixed a crash found on the way: destroying the partner's map marker while
  the host travelled into a mission crashed the game; the marker is now
  hidden instead.
- Item 5/15, mission weather done: the guest streams the host mission's
  weather level (`PerformActionOnStreamingLevels`) and applies the mission's
  sky preset; the guest's view matched the host's fog in `OBR`.
- Item 11: far teleports now wait for ground; the post-load teleport waits
  for the host's cinematic; the guest no longer follows the host into
  mission saves (resync deferred until the host's first save after the
  mission). A real region-transition test needs a save that has reached
  `RNA1`/`LRH1`; out-of-order starts from the act-2 save leave the guest in
  locked regions.
- Item 5, companion look: the host sends each companion mesh's slot
  materials and their scalar overrides (`DMMT`); the guest sets them on
  dynamic instances. Boozer's shirt was invisible without the host's
  `HC_ON=0`. In `OBR` the guest's crops show Boozer fully textured. Left for
  item 5: other companions, and Boozer's bike (item 16).
- Item 16: vehicles near the host other than its own bike are streamed as
  blueprint class plus transform (`DMVH`), and the guest spawns the same
  class for display. In `OBR` the guest saw Boozer's bike riding off with
  Boozer and the host and parked next to the host. Open: the ride prompt on
  the replica, mission trucks, wheel animation.
