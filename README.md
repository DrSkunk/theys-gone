![Theys Gone](./docs/theys_gone.avif)

# Theys Gone — Windows Enhanced Co-Op Edition

[![CI](https://github.com/Mohamed-Moslem-Allouch/theys-gone/actions/workflows/ci.yml/badge.svg)](https://github.com/Mohamed-Moslem-Allouch/theys-gone/actions/workflows/ci.yml)
[![Upstream](https://img.shields.io/badge/Upstream-DrSkunk%2Ftheys--gone-blue)](https://github.com/DrSkunk/theys-gone)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows_10%2F11-green)](https://github.com/Mohamed-Moslem-Allouch/theys-gone)

**Theys Gone (Windows Enhanced Edition)** is a customized, high-performance fork of the experimental two-player co-op modification for **Days Gone (PC)**, developed and maintained by **Mohamed Moslem Allouch** ([@Mohamed-Moslem-Allouch](https://github.com/Mohamed-Moslem-Allouch)).

This fork builds directly on the foundational engine architecture created by **[DrSkunk](https://github.com/DrSkunk/theys-gone)**, with targeted engineering focused on **real-world Windows hardware validation, high-refresh-rate (144Hz+) decoupled interpolation, low-latency residential P2P networking, and enhanced Slate menu controls**.

> [!NOTE]
> **Upstream Collaboration:** This project works in ongoing synchronization with [DrSkunk/theys-gone](https://github.com/DrSkunk/theys-gone). Foundational engine fixes developed in this fork (such as the 256-slot `VirtualQuery` memory cache, `GNames` miss backoff, and the passenger ride prompt actor lifecycle fix) have been submitted upstream (see [PR #10](https://github.com/DrSkunk/theys-gone/pull/10) and [PR #15](https://github.com/DrSkunk/theys-gone/pull/15)).

---

## What Makes This Edition Special?

While upstream focuses primarily on Linux/Proton headless testing environments, this fork is **engineered and stress-tested in live, 2-player co-op sessions on native Windows hardware**:

### 1. Zero-Stutter Windows Engine Performance
- **Kernel-Level Memory Caching:** Implements a direct-mapped 256-slot cache (`ReadableRegionCache`) for Win32 `VirtualQuery` calls, eliminating thousands of Ring-0 kernel context switches per second that freeze the game on Windows.
- **Global Name Table (GNames) Optimization:** Caches unresolved names with exponential retry backoff, completely eliminating the 19.6-second game freeze during world loading and UI initialization.
- **144Hz+ Decoupled Visual Interpolation:** Decouples character and motorcycle visual transforms from the fixed 60Hz network tick. Smoothly renders partner animations at your monitor's native refresh rate (144+ FPS).
- **TAA Anti-Ghosting:** Integrates native UE4 Temporal Anti-Aliasing velocity vector reprojection (`bTeleport: false`), eliminating motion blur smearing and foot shadow trails behind running characters.

### 2. High-Efficiency P2P Networking & Latency Elimination
- **Kinematic Bone Whitelisting:** Selectively streams ~140 essential kinematic joints while pruning ~350 micro-facial deformation bones. Drops pose bandwidth from 136 pkt/s to ~30 pkt/s, completely eliminating Steam P2P socket buffer overflows (`result 25 k_EResultLimitExceeded`) and partner movement rubberbanding.
- **Procedural IK Bone Alignment:** Streams pure rotational quaternions for child bones while preserving root translation. Weapon aiming sights and motorcycle handlebar grips align naturally without collapsing or twisting character meshes.
- **Steam P2P Relay (Spacewar AppID 480):** Zero-configuration peer-to-peer relay networking without requiring manual port forwarding or external VPNs.
- **Multi-Channel Steam Invite Pipeline:** Integrated Steam In-Game Overlay invite dialog (`ActivateGameOverlayInviteDialog`), desktop Steam friends list trigger, and direct join link clipboard copying.

### 3. Native Slate UI & Cheats Submenu
- **Input Debounce & Anti-Loop:** Removed concurrent `GetAsyncKeyState` mouse polling in `PauseUpdate` and added a 350ms input debounce latch, eliminating repetitive audio clicking loops and accidental lobby disconnects.
- **Dedicated Cheats Submenu:** Accessible directly from the in-game gameplay pause menu under `CO-OP → CHEATS`:
  - **God Mode** (Invulnerability toggle)
  - **Infinite Stamina** (Sprinting & roll toggle)
  - **Repair & Refuel Bike** (Instant full tank & 100% condition)
- **Menu Layout Protection:** Strictly clamps rendered menu items to 10 rows to prevent UE4 `OptionsCategoryWidget` bounds overflows and hidden text.

### 4. Passenger Stability & Crash Prevention
- **Persistent Interaction Actors:** Replaced rapid Blueprint actor spawn/destroy loops with persistent actor show/hide toggles (`SetActorHiddenInGame`), eliminating fatal `0xC0000005` access violations when boarding motorcycles.
- **Full Atomic World Save Transfer:** Automatically bundles `autosave*.sav` + `checkpoint.sav` + `profile.sav` into an atomic payload, ensuring guests spawn directly into the host's active campaign without being kicked to a "New Game".

---

## Master Co-Op Roadmap

| Phase | Focus Area | Status | Key Deliverables |
|:---:|---|:---:|---|
| **Phase 1** | **Movement & Physics** | **COMPLETED** | 144Hz interpolation, clothing sync, bone streaming, bike/passenger sync, save transfer, Spacewar P2P. |
| **Phase 2** | **RPCs, Audio & Anim** | **IN PROGRESS** | Flashlight & headlight sync, vehicle collisions/impacts, gunshot audio, muzzle FX, weapon reload anims. |
| **Phase 3** | **World Triggers & Doors** | *Planned* | Synchronized gates, quest doors, bunkhouse beds, NERO checkpoint generators. |
| **Phase 4** | **Time & Weather** | *Planned* | Host-authoritative 24h world clock, synchronized rain, snow accumulation, fog, and mud physics. |
| **Phase 5** | **AI & Horde Sync** | *Planned* | Synchronized Freaker swarms and Horde density, unified aggro triggers, marauder camp combat AI. |

---

## Quick Start (Installation)

### Prerequisites
- **Game Version:** Days Gone on PC (Steam build `19221447`).
- **Loader:** [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) x64 installed as `version.dll` in `BendGame/Binaries/Win64/`.

### Installation Steps
1. **Back up your save files** from `%LOCALAPPDATA%\BendGame\Saved\`.
2. Copy `TheysGone.asi` and `TheysGone.ini` into `BendGame/Binaries/Win64/` (next to `DaysGone.exe`).
3. *(Optional Branding)* Copy `pakchunk99-TheysGone_Brand_P.pak` into `BendGame/Content/Paks/` to enable custom title and main menu logos.
4. Launch the game on both PCs.

---

## How to Play

### Hosting an Online Game (Steam P2P)
1. In the title menu or in-game pause menu, select **CO-OP**.
2. Click **HOST ONLINE GAME**.
3. Click **INVITE FRIENDS** to open the native Steam overlay invite dialog and invite your friend directly.
4. Click **START GAME** to begin playing.

### Joining a Host
1. Accept the Steam invitation sent by your friend, or open **CO-OP → JOIN A FRIEND** in the title menu.
2. The host's world save will automatically transfer and load seamlessly.

---

## Controls Reference

| Action | Keyboard / Mouse | Gamepad (XInput) |
|---|---|---|
| **Open CO-OP Menu** | Title / Pause Menu or `F8` | Pause Menu / View+Y |
| **Board / Ride Partner's Bike** | Hold Interact (`E`) or `F9` | Hold `Square` / `X` |
| **Teleport to Partner** | `F7` (`TeleportKey`) | View / Back Button |
| **Emergency Recall Bike** | In CO-OP Menu | In CO-OP Menu |
| **Toggle Cheats** | Pause Menu $\rightarrow$ CO-OP $\rightarrow$ CHEATS | Pause Menu $\rightarrow$ CO-OP $\rightarrow$ CHEATS |

---

## Building from Source (Windows)

Unlike upstream which utilizes Linux/Proton Makefile scripts, this fork includes a standalone Windows build script that compiles directly with MinGW GCC:

```cmd
python build_win.py
```

The script compiles all C++20 modules, links with Windows system libraries, and outputs the optimized `TheysGone.asi` binary into the `package/` folder.

---

## Credits & Acknowledgments

- **[DrSkunk](https://github.com/DrSkunk/theys-gone)**: Original creator and maintainer of the upstream *Theys Gone* project. Thank you for the foundational reverse-engineering research, reflection invocation architecture, and ongoing open-source collaboration.
- **Mohamed Moslem Allouch** ([@Mohamed-Moslem-Allouch](https://github.com/Mohamed-Moslem-Allouch)): Windows real-hardware profiling, 144Hz decoupling, bone bandwidth optimization, Slate input debounce, and cheats subsystem.
- **Aziz** (`alloucheaziz25`): Dedicated co-op partner and live stress-test operator.
- **Bend Studio & Sony Interactive Entertainment**: Creators of *Days Gone*.

---

## License

This project is licensed under the [MIT License](LICENSE). Days Gone is © Sony Interactive Entertainment and Bend Studio. This is an unofficial, non-commercial fan modification.
