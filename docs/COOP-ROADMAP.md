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
2. [ ] **Guest treats a mission it is not running as active.** The guest's
   `ActiveMissionData` stays set without a mission script, so the guest
   applies mission policies (passenger seat, stealth) for a mission it does
   not play. Use "mission script running" for local policy. Verify: guest
   `status` reports no mission after following the host.
3. [ ] **Partner on the minimap and the world map.** Each game shows the
   other player with a `MiniMapLocationComponent` marker that follows the
   partner avatar. First find a friendly icon in use (`minimap-probe`).
   Verify: screenshots of both minimaps and maps show the partner marker at
   the right place.
4. [x] **Corrections to merged docs.** `STORY-COOP.md` and the research log
   still say the guest "runs its own copy of the mission with its own
   Boozer"; that was based on host screenshots mislabelled as the guest's.
5. [ ] **Host's companions and mission vehicles on the guest.** The guest
   cannot run its own copy of a mission, so it sees the host riding with
   nobody. Stream non-hostile mission actors (Boozer, Rikki, Sarah, their
   bikes, the turret truck) as display-only replicas, like enemies. Protocol
   change. Verify: the guest sees Boozer next to the host in `OBR`.
6. [ ] **Partner avatar during cutscenes and flashbacks.** Hide the partner
   while the local game plays a cinematic or a flashback mission (`WNG`,
   `HGL`, `TSR`, `IPR`, `AQW`); check what the guest sees while the host is
   in a cutscene. Verify: screenshots from both games during `OBR`'s intro.
7. [ ] **Protocol v9 guest-side proxy** (AGENTS gap 1): guest-kept enemies
   targeting and damaging the host after a regroup. Now testable with two
   real games. Verify: proxy targeting on the guest and a host health drop.
8. [ ] **Pillion character joining mid-mission** (Lisa in `RBC`): the seat
   rule while the host rides with the guest before Lisa boards.
9. [ ] **Stealth: enemies already fighting** still switch to the partner;
   confirm nothing else alerts the camp (native sight of the stand-in).
10. [ ] **Capture missions** (`CRW`, `TLL2`, `PBC4`/`AON1`, `AHB5`): what the
    guest does while the host is captured; candidate policy in STORY-COOP
    §4.5.
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
