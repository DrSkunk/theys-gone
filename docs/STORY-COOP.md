# Story and companions: co-op design map

Who is with Deacon in each part of the campaign, which segments assume he is
alone, and what co-op needs there. **This is design research, not co-op
evidence:** nothing below has been tried in a paired session.

## Sources

1. **Mission catalog (authoritative for names and objectives).** The
   `mission-state` debug command writes every job and story mission with its
   display name, act, map and objective texts (see
   [REPLICATION-RESEARCH.md](REPLICATION-RESEARCH.md), "Story mission
   catalog"). Mission codes, names, acts and objectives below come from it.
2. **Game assets.** Cinematic tracks are stored per actor
   (`Cinematics/Act<n>/<mission>/<scene>/<actor>`), which gives the cast of
   every cutscene and which bikes appear in it. Each mission streams its own
   sublevel (`Maps/ORWorld/Missions/Part<n>/<code>…`), usually with a
   `<CODE>1_Weather` level.
3. **Walkthroughs** (PowerPyx, GameWith, mapmaster.io, Wikipedia) only for
   gameplay details the data does not show (forced stealth rules, captures,
   points of no return). Those details are marked *(walkthrough)*.

## 1. Acts and region gates

`BendMissionData.MissionsAct` is the story episode; chapter titles come from
`GetChapterNameFromEpisode`.

| Act | Chapter | Regions | Starts with | Ends with |
|---|---|---|---|---|
| 2 | The Drifter Road | Cascade, then Belknap (`TUB`) | `HAT` He Can't Be Far | `TLL1` No One Saw It Coming (ride south) |
| 3 | Wings Of Angels | + Lost Lake | `TLL2` Not Gonna Kill Anyone | `NRS2` You Could Have Done More |
| 4 | A Camper's Life | + Iron Butte | `MBF1` Not Like I Got a Choice | `RNA1` Riding Nomad Again, **point of no return** (south) |
| 5 | The Forsaken | Crater Lake, Highway 97 | `MUF1` Mayday! Mayday! | `IYK` This Could Be It |
| 6 | Lost Life | same | `LTB1` You Alone I Have Seen | `AHB5` Shadow of Death (**point of no return**), `LRH1` ride north |
| 7 | Rest In Peace | north again | `WBB1` Keep Them Safe | `AWI2` For an Outlaw Biker (credits) |
| 12 | (post-credits) | all | `GMR`, `GOL`, `IMG1`, `NEO` | — |

Acts 17 (E3 demo) and 18 (DLC challenges, arcade, benchmark) are not story.

## 2. Mission categories

| Cat. | Meaning |
|---|---|
| **F** | Free fight or open objective. Fits co-op as-is |
| **E** | Companion rides or walks with Deacon on their own |
| **P** | Companion rides **on Deacon's bike** (or Deacon on theirs) |
| **G** | Guard: the companion must survive |
| **S** | Forced stealth; detection fails the mission or checkpoint |
| **C** | Capture or incapacitation |
| **B** | Bike lost, replaced or borrowed |
| **M** | Memory or flashback (past timeline) |
| **X** | Boss or scripted set piece |
| **T** | Region transition or point of no return |
| **K** | Talk or cutscene only |

## 3. Story missions

Story order is the mission ID. "Companions" come from objective texts, plus
cutscene cast where the objectives do not name anyone. "Bikes" are the bike
actors in that mission's cutscenes.

### Act 2: The Drifter Road

| ID | Code | Mission | Companions | Key objectives | Cat. |
|---|---|---|---|---|---|
| 1 | `HAT` | He Can't Be Far | Boozer; Leon, Alvarez (bikes for all four) | Chase Down Leon | E X |
| 2 | `BMB` | We'll Make It Quick | Boozer, Leon | Track Leon, Return To Bikes, craft and use a bandage | E |
| 3 | `OBR` | I Say We Head North | **Boozer** (level name "riding bitch on Old Belknap Road") | Ride With Boozer to Crazy Willie's, Clear the Road | **P** E B |
| 4 | `OBT` | Bad Way to Go Out | Boozer | Walk Point Through the Tunnel, Burn the Freaker Nest, Get On the Bike, Ride With Boozer | E F |
| 5 | `CRW` | You Got a Death Wish | Boozer captured | Find Fuel Pump, Find Boozer, Save Boozer From Rippers | C F |
| 6 | `WMB` | Drifters on the Mountain | Copeland's man, Copeland, Manny | Follow The Marauder, Clear The Marauder Camp, Follow Copeland's Man, Walk With Copeland; new bike | **B** E F |
| — | `IND` job | Bugged the Hell Out | Boozer (end) | NERO checkpoint generator, Take Sterile Bandages To Boozer | F |
| — | `BAC` job | She Rode With Us | — | Burn Alvarez's Corpse | F |
| 7 | `NBE` | Out of Nowhere | — | Pioneer Cemetery, Find Leon's Stash, Tie Stash to Bike, helicopter sighting | S *(walkthrough: invulnerable NERO soldiers)* |
| 8 | `WAN` | Searching for Something | — | Chase the NERO Helo, Get the NERO Radio | X |
| 9 | `WNG` | What's a Nice Girl | **Sarah (past)**, O'Brian; flashback bike | Give Sarah A Ride To Crazy Willie's, Give Sarah A Ride Back To Her Car | **M P** |
| — | `WFD` job | What Did You Do? | Sarah (memorial) | Clear Camp Of Freakers, Place Stonecrop | F M |
| 10 | `CTR` | It's a Rifle, Not a Gun | Copeland | Track and shoot the deer with Copeland's gun | E F |
| 11 | `HGL` | I've Pulled Weeds Before | **Sarah (past)**; Boozer's fever | Help Sarah Gather Lavender | **M** |
| — | `HGL2` job | Give Me a Couple Days | Boozer | Find Lavender, Return To Boozer | K |
| 12 | `TUB` | Price on Your Head | Tucker, Alkai | Ride With Tucker, Find Alkai | E **T** |
| — | `DTC` job | Deliver to Copeland | Copeland | Deliver the Drugs to Copeland | K |
| 14 | `LOL1` | It's Not Safe Here | Tucker | Track the Survivor, Clear Marion Forks Of Rippers | F |
| 15 | `LOL2` | Lots of Sick People | **Lisa** | Return To Your Bike With Lisa, Defeat The Bear, Ride To The Hot Springs, Defeat the Marauders | **P** X F |
| 16 | `COL` | They Won't Let Me Leave | Lisa | Find Lisa In the Hot Springs Encampment | K |
| 17 | `TFL2` (+`TFL` job) | I Brought You Something | Lisa | Find and bring a Thunderegg | K |
| 19 | `RBC` | What Have They Done | **Lisa**, Rikki (own bike) | Free Lisa, Get Lisa to Your Bike, Ride With Lisa To Meet Rikki | **P** F |
| 20 | `FIN` | They're Not Sleeping | — | Follow and Spy On the NERO Researcher | **S** |
| 26 | `OBF` | Making Contact | O'Brian | Infiltrate the NERO Landing Zone, Follow O'Brian | **S** K |
| 21 | `TLL1` | No One Saw It Coming | Boozer | Walk With Boozer To Your Bike, Ride South To Lost Lake (`BP_BikeSpeedController` in the level) | E **T** |

### Act 3: Wings Of Angels (Lost Lake)

| ID | Code | Mission | Companions | Key objectives | Cat. |
|---|---|---|---|---|---|
| 22 | `TLL2` | Not Gonna Kill Anyone | — | Sneak Into the Camp and Infirmary, Steal the Antibiotics; caught on exit | **S C** |
| 23 | `TLL3` | No Place Else to Go | Rikki, Boozer | Walk With Rikki, Drive Back to Lost Lake | E |
| 24 | `TLL4` | We've All Done Things | Rikki, Iron Mike; Boozer's amputation | Walk With Rikki, Walk With Iron Mike | K |
| — | `FLK` job | Sherman's Camp Is Crawling | Addy | Get the Liston Knife | F |
| 27 | `SFN` | I Need Your Help | O'Brian | Place GPS Tracker On the NERO Helo, Spy On the NERO Researcher | **S** |
| 28–30 | `OJ1`–`OJ3` | It's on a Mission, On Herod's Birthday, Moments of Lucidity | — | GPS tracker, Spy On the NERO Researcher | **S** |
| 31 | `NFS1` | Now You See It | Skizzo | Walk With Skizzo to the Lost Lake Lodge | E |
| 33 | `NFS4` | Playing All Night | — | Kill the Breaker, retrieve antibiotics | **X** |
| 34 | `NFS6` | With Other Men's Blood | Rikki | Ride With Rikki To the Lost Lake Encampment | E |
| 35 | `FMC1` | Flow Like Buried Rivers | Iron Mike | Kill The Swarmers, Walk With Iron Mike, Tail the Horde | E X |
| 36 | `FMC2` | You See What They Did | Iron Mike | Ride With and Follow Iron Mike, courthouse | E |
| 37 | `FMC3` | Do You Have My Back? | Iron Mike | Ride With Iron Mike | E |
| — | `COB1` job | I Could Use a Hand | Boozer | Check On Boozer | K |
| 38–42 | `WAD1`, `WAD2`, `WADN`, `WADR`, `WAD3` | I Got a Job for You → It Couldn't Be That Easy → They Don't Like Visitors → That's When I Knew → It's a Long Story | **Rikki** (own bike) | Ride North With Rikki, turbine, nests, Rippers, Ride South With Rikki | E F |
| — | `RBB` job | Riding the Open Road | Boozer | Get on Boozer's Bike | **B** K |
| 43 | `BTF` (+`COB2` job) | I Got Work to Do / The Only One He's Got | Boozer, Addy | Check On Boozer At the Farm | K |
| 44–47 | `MBU1`–`MBU4` | Some Kinda Freak Expert → Lines Not Crossed → We're Not Hiding → That's His Mistake | **Skizzo** (own bike) | Race Skizzo to Lucky Lad Mine, Explore the Mine With Skizzo, Get TNT, Clear Marauders | E F |
| 48 | `BIP` | Drinking Himself to Death | **Boozer** (found) | Search farms, Track Boozer, Escape to Lost Lake, Return Boozer To the encampment | P? F |
| — | `CBA`, `CBA2` jobs | About Boozer's Arm, Was This a Good Idea? | Rikki | Gather parts for Boozer's arm | F K |
| 49 | `NRS1` | You Twisted My Arm | — | Infiltrate the NERO LZ, Spy | **S** |
| 50 | `NRS2` | You Could Have Done More | O'Brian, Boozer | Ride To O'Brian, Return To Boozer | K |

### Act 4: A Camper's Life (Lost Lake, Iron Butte)

| ID | Code | Mission | Companions | Key objectives | Cat. |
|---|---|---|---|---|---|
| 51 | `MBF1` | Not Like I Got a Choice | **Boozer** | Track Boozer, **Keep Boozer Alive**, Take Boozer Back to Work | **G** F |
| 53 | `LBL1` | Better to Light One Candle | **Rikki** (own bike) | Ride with Rikki, Burn the Freaker Nest, **Protect Rikki** | E **G** |
| 54 | `LBL2` | Outta the Darkness | — | Head To the Lost Lake Infirmary | K |
| — | `PBC1` job | Trying to Help the Camp | Skizzo | Meet Skizzo | K |
| 56–58 | `PBC2`–`PBC4` | Don't Get Caught → They Don't Feel Pain → It Was the Only Way | **Skizzo**, on foot | Follow and Walk With Skizzo, Clear All Newts, Open the Door For Skizzo; betrayal | **B S C** |
| 59 | `AON1` | I Kept My Name | Lisa | **Escape the Ripper Temple, Find Your Gear**, Retrieve Your Mongrels Cut, Rescue Lisa | **C S** |
| 60 | `AON2` | Should Have Seen It Coming | — | Return To Your Bike | **B** |
| 61 | `ALL1` | Riders Sent to Find You | Iron Mike's bikers | Ride With Iron Mike's Bikers | E X |
| 62 | `ALL2` | They Will Never Stop | — | Rescue the Hostages | F X |
| — | `BBD2`, `TSK` jobs | You Won't Be Needing This, Like Your New Digs? | Skizzo | Skizzo's cabin, Taunt Skizzo | K |
| 63 | `BBD1` | Without Being Seen | — | Return Explosives To Your Cabin | S? |
| 64 | `BBD3` | Now That's an Idea | Boozer | Ride with Boozer to the Dam, Kill the Rippers | E F |
| 65 | `BBD4` | That Never Gets Old | **Boozer** | Get To the Sniper Tower, **Protect Boozer From Rippers** | **G** X |
| 66 | `BBD5` | Time for Some Payback | — | Enter the Ripper Lodge, Clear the Area | F |
| 67 | `BBD6` | I'm Good With That | Carlos | Kill Carlos (knife duel *(walkthrough)*) | **X** |
| 68 | `TSR` | I Was Distracted | **Sarah (past)**, Rikki, Addy; flashback bike | Ride With Sarah | **M P** |
| — | `CTS` job | Why Am I Here? | O'Brian | Meet O'Brian | K |
| 69 | `AQW` | No Beginning and No End | Boozer, Sarah (wedding memory) | Clear the Squatters | M F |
| — | `RTL1`, `RTL2` jobs | Something to Heal His Soul, Have It Your Way | Addy, Boozer | Capture the Dog | F |
| — | `SHS*` jobs | Sarah's memorial stone visits | Sarah | Visit Sarah's Memorial Stone | M K |
| 70 | `RNA1` | Riding Nomad Again | Boozer, **Iron Mike** (own bike) | Talk To Boozer, Ride with Mike to the Thielsen Pass | E **T** |

### Act 5: The Forsaken (Crater Lake, Highway 97)

| ID | Code | Mission | Companions | Key objectives | Cat. |
|---|---|---|---|---|---|
| 71 | `MUF1` | Mayday! Mayday! | — | Kill The Rager | X |
| 72 | `MUF2` | Not From Around Here | Corporal Russell | Ride With Corporal Russell | E |
| 73–76 | `ANM1`–`ANM4` | We're Fighting a War → Prove It to Me → Driven to Extinction → Don't Give Me Orders | **Captain Kouri** (own bike) | Walk and Ride With Kouri, Track the AWOL Soldier, Sneak To the Lumber Mill, Rescue the Hostage | E F S |
| 77 | `JWI1` | A Target on Their Backs | Kouri | Ride South With Captain Kouri | E |
| 78 | `IPR` | I Promise to Never Leave You | **Sarah (past)** | Walk With Sarah | **M** |
| 79 | `JWI2` | I Don't Have a Pic | Kouri | Ride To Wizard Island With Captain Kouri | E |
| 80 | `TCA1` | A War We Can Win | Colonel Garret; Sarah reunion, Weaver | Walk With Colonel Garret | K |
| 81–85 | `SOM1`–`SOM5` | We Do Not Discriminate … Evil Surrounds Us | Colonel Garret | Listen To Colonel Garret's Speech | K |
| — | `WJ1`, `WJ2`, `SJ1`, `SJ2`, `COD1`–`COD3`, `MJ1`, `TIN`, `TDD1` jobs | Weaver, Sarah, Doc Jimenez, Garret and Taylor jobs | as named | fetch jobs, napalm test, Rescue Taylor | F K |
| 86 | `RFS1` | Just Another Requisition Form | **Sarah** | Follow Sarah To Her Tent, **Follow Sarah To Your Bike** | **P** |
| 87 | `RFS2` | Can I Ask You Something? | **Sarah** | Ride With Sarah To the Chemult Community College | **P** |
| 88 | `RFS3` | So Many of Them | Sarah | Clear the Swarm, Find Sarah, Help Sarah Find the Centrifuge | F |
| 89 | `RFS4` | You Couldn't Stop Shaking | **Sarah** | Retrieve The Centrifuge, Return To Your Bike With Sarah | **P** F |
| 90 | `CLO2` | What Kept Me Going | **Sarah** | Ride To the Thielsen Pass With Sarah, Ride North To Cloverdale | **P** |
| 91 | `CLO3` | I Knew These People | Sarah | Walk With Sarah | E |
| 92 | `CLO4` | Expect the Worst | Sarah | Clear the Warehouse, Boost Sarah To the Roof, Clear the Atrium | E F |
| 93 | `CLO5` | We Couldn't Take the Risk | — | Turn Off the Electric Fences, Burn the Crier Nests | F |
| — | `COB3` job | Just Checking In | Boozer (radio) | Clear the Ambush Camp, Radio Boozer | F |
| 94 | `IYK` | This Could Be It | — | Tranquilize and Collect Sarah's Test Subject | **X** |

### Act 6: Lost Life

| ID | Code | Mission | Companions | Key objectives | Cat. |
|---|---|---|---|---|---|
| 95 | `LTB1` | You Alone I Have Seen | Kouri, Sarah, the Colonel | Clear the Chemult Horde, Return To Captain Kouri | **X** |
| — | `MJ2`, `BLC1`, `WJ3`, `COB4` jobs | How Far We've Fallen, I've Got a Plan, He's Not Big on Tunes, Makes No Difference to Them | Garret, Sarah, Weaver, Boozer | fetch jobs, ambush camp | F K |
| 96 | `ICF1` | What It Takes to Survive | O'Brian, Cooper | Explore the Ice Wind Lava Cave, Kill the Reacher, Clear the Horde | **X** |
| 99 | `AHB5` | Shadow of Death | **Sarah** | Disable the RPG, **Take Sarah To the Top Of the Volcanic Cone**; caught, hanging *(walkthrough)* | **G S C T** |
| 100 | `LRH1` | Ascending From the Underworld | Kouri (farewell) | Ride to the Lost Lake Encampment | **T** |

### Act 7: Rest In Peace, and post-credits

| ID | Code | Mission | Companions | Key objectives | Cat. |
|---|---|---|---|---|---|
| 103 | `WBB1` (+job) | Keep Them Safe | Rikki, Boozer (radio) | Ride To Iron Butte Ranch, Clear the Horde, Radio Boozer | **X** |
| — | `WBB2` job | I'll Save Some for You | Boozer drives a turret dump truck (cutscene) | Clear the Horde At the Old Sawmill, Fill Truck With Creosote | **X** |
| 104 | `AWI1` | You Can't Do This Alone | Boozer, Copeland, Manny, Alkai, Rikki, Addy (cutscenes) | Fight Your Way Into the Ark, Disarm the Explosives | E X |
| 105 | `AWI2` | For an Outlaw Biker | Weaver, Skizzo, the Colonel | Save Weaver, Kill Skizzo, Get to the Colonel's Chamber | **X** |
| — | `GMR` job | Where's My Damn Rings? | — | Find Your Rings | K |
| — | `GOL` job | I'm Not a Ripper | Lisa | Head to Iron Mike's | K |
| — | `IMG1` job | For the Benefit of Others | Rikki | Meet Rikki At the cemetery | K |
| — | `NEO` job | There's Nothing You Can Do | O'Brian | Ride To the NERO LZ (secret ending) | K |

IDs 13, 18, 25, 32, 52, 55, 97, 98, 101, 102 and 106 return no mission data.
`STA`, `BLL` and `LOR` exist only as cinematics (ending, Lost Lake battle,
camp rally).

### Free-roam job families

| Prefix | Meaning |
|---|---|
| `<REGION>_AZ_nn` (`CA`, `BE`, `LL`, `IB`, `CL`, `HW`) | Ambush camps (`_Reset` variants re-arm them) |
| `IZ_<place>` | Infestation zones |
| `Horde<code>`, `AmbientJobHorde…`, `WS<region>` | Hordes |
| `NERO_<region>_<place>` | NERO checkpoints |
| `<camp>_<type>_nn` (`CC` Copeland, `HS` Hot Springs, `IM` Iron Mike, `DL` Diamond Lake, `WI` Wizard Island) | Camp jobs: bounties, enemy camps, survivor rescue, bike chases, hunting; `_CJ_` are radio-call chains |
| `HS_EJ_01` | Bike escort (`BP_BikeEscortJobScriptBase`) |
| `BZ_*` | Radio towers and bounty zones |

## 4. Co-op problems

1. **Missions run only on the host.** When the host starts a mission that
   saves at its start, the guest reloads that save through the story resync,
   but the mission does not start there: the guest gets `ActiveMissionData`
   without a mission script, and the mission's start fade stayed black until
   the mod lifted it (paired `OBR` tests, 2026-10-29). The guest therefore
   has none of the mission's NPCs, sublevel props, forced weather or
   objectives. Companions are excluded from enemy streaming, so the guest
   sees the host riding or walking next to nobody. Fix: stream
   non-hostile mission actors (companions, their bikes, mission trucks) as
   display-only, non-colliding replicas, and send mission weather.
2. **Passenger seat conflicts (P).** Lisa (`LOL2`, `RBC`) and Sarah (`WNG`,
   `TSR`, `RFS1`/`RFS2`/`RFS4`, `CLO2`) ride on Deacon's bike, and in `OBR`
   Deacon rides on Boozer's. While a story NPC uses the host's pillion, the
   guest must not be offered *Ride with*. **Implemented:** a driver whose
   running mission has the pillion flag stops offering the seat
   ([`mission_policy.hpp`](../src/coop/mission_policy.hpp)); verified in a
   paired session: a riding guest was let off when the host started `OBR`. `BIP`, `MBF1`, `CLO5` and `IYK`
   are flagged from cutscene bikes and the walkthrough, not from objective
   text. Other ride-alongs use the companion's own bike
   (`boozerbike`, `rikkibike`, `skizzobike`, `mikebike`, `kouribike` in the
   cutscenes), so the guest on their own bike fits. Scripted rides such as
   `TLL1` (`BP_BikeSpeedController`) pace to the host only.
3. **Forced stealth (S) and the hidden proxy.** Host enemies are made aware
   of the hidden `SwarmerPawn_C` following the guest
   (`SetAwarenessOfPawn`). In S missions (`NBE`, `FIN`, `OBF`, `SFN`,
   `OJ1`–`OJ3`, `NRS1`, `TLL2`, `PBC2`–`PBC4`, `AON1`, `ANM3`, `AHB5`) the
   guest can get the host detected. **Confirmed and fixed:** in a paired
   `AON1` test nine of 13 Rippers targeted the stand-in while nobody moved;
   with the fix, enemies in S missions are told about the stand-in only once
   they are already fighting, and none targeted it.
4. **Guard objectives (G).** "Keep Boozer Alive" (`MBF1`), "Protect Rikki"
   (`LBL1`), "Protect Boozer From Rippers" (`BBD4`), escorting Sarah
   (`AHB5`). These suit co-op if guest hits apply on the host's enemies,
   which already works.
5. **Captures (C).** `CRW` (Boozer), `TLL2` (caught), `PBC4` → `AON1` (Deacon
   betrayed, gear and cut taken), `AHB5` (hanging). A free, armed partner
   contradicts them. Candidate policy: while one is active, hide the guest's
   avatar on the host, stop guest hits on mission enemies, and move the
   guest to the host when it completes.
6. **Flashbacks (M).** `WNG`, `HGL`, `TSR`, `IPR`, `AQW` and the memorial
   stone jobs play in the past or in memory. The partner must not appear.
   `BendGameMission.OnFlashbackCinematicToggled` may signal the cinematic
   part; playable sections need the policy table.
7. **Bike loss (B).** After `OBR` and through `WMB` the host's bike is
   broken or stolen; passenger riding with the guest driving fits. In
   `PBC2`–`AON2` the guest should leave their bike too.
8. **Bosses and set pieces (X).** The Carlos knife duel (`BBD6`) and Skizzo
   (`AWI2`) are single-fighter scenes; hordes (`FMC1`, `LTB1`, `ICF1`,
   `WBB1`, `WBB2`) and Ragers suit two, but scripted hordes likely exceed the
   16-enemy stream cap.
9. **Region transitions (T).** After `RNA1` (south) and `LRH1` (north) the
   guest's world changes only at the next host save. Candidate policy: when
   the host's act changes, move the guest to the host after the reload.
10. **Cutscenes (K).** The partner avatar is not hidden during cinematics.
    The host's view of a paired `OBR` intro showed no second Deacon; the
    guest's view was not captured (see the research log). If a stray avatar
    appears, hide it while that game's `GetIsCinematicActive` is true.

## 5. Next steps

Done: `BendGameMission.ActiveMissionData` names the running mission (checked
by starting `OBR`); the per-mission table is
[`mission_policy.hpp`](../src/coop/mission_policy.hpp), covered by
`tools/test_protocol.cpp`; the passenger-seat rule (§4.2) uses it.

1. Show the host's mission to the guest (§4.1): stream companions and
   mission vehicles as display-only replicas, and send the mission weather.
   Tracked in [COOP-ROADMAP.md](COOP-ROADMAP.md).
2. Check whether enemies that are already fighting still switch to the
   partner in stealth missions (§4.3).
3. Send the host's active mission code to the guest (a protocol change), so
   the guest can apply the flashback, capture and cutscene policies.
4. Stream companions and mission vehicles as display-only replicas (§4.1).
