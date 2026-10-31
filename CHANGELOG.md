# Changelog

## [0.6.0](https://github.com/DrSkunk/theys-gone/compare/v0.5.1...v0.6.0) (2026-10-31)


### ⚠ BREAKING CHANGES

* coordinate co-op menu pause and weapon-wheel timing ([#23](https://github.com/DrSkunk/theys-gone/issues/23))
* protocol v10 (state packet gains storyMission) and DMEN v2; both players need this release.

### Features

* coordinate co-op menu pause and weapon-wheel timing ([#23](https://github.com/DrSkunk/theys-gone/issues/23)) ([90a03af](https://github.com/DrSkunk/theys-gone/commit/90a03af131c6b6b6674f49ce71675f577f9047b1))
* **enemies:** companion replicas wear the host's materials ([#20](https://github.com/DrSkunk/theys-gone/issues/20)) ([34fc543](https://github.com/DrSkunk/theys-gone/commit/34fc543fd6d8ff2a53ae58940ac3ab499d811ac7))
* guest follows the host's missions from the open world (protocol v10) ([#19](https://github.com/DrSkunk/theys-gone/issues/19)) ([07e7573](https://github.com/DrSkunk/theys-gone/commit/07e75737e18fd3010a2df6eba472bed280c6979a))
* **steam:** optional Spacewar (app 480) lobbies ([#16](https://github.com/DrSkunk/theys-gone/issues/16)) ([417ad05](https://github.com/DrSkunk/theys-gone/commit/417ad055c2060b50d8d6fa9fedde6116fb82af60))
* story mission map and passenger seat rule for co-op ([#11](https://github.com/DrSkunk/theys-gone/issues/11)) ([138199c](https://github.com/DrSkunk/theys-gone/commit/138199c4f53b5c790c83fbb6207e2820b7a1743e))
* **vehicles:** show vehicles near the host, such as Boozer's bike ([#21](https://github.com/DrSkunk/theys-gone/issues/21)) ([cf8cc8a](https://github.com/DrSkunk/theys-gone/commit/cf8cc8a6517cfc6dc86b268a1983f475febcc90a))


### Bug Fixes

* cap adopted-save reloads; mission progress log; lab fixes ([#18](https://github.com/DrSkunk/theys-gone/issues/18)) ([af18bac](https://github.com/DrSkunk/theys-gone/commit/af18bacd498d29fa1f4835453a411499c4c90fe0))
* stop the partner stand-in from alerting enemies in stealth missions ([#14](https://github.com/DrSkunk/theys-gone/issues/14)) ([49944c5](https://github.com/DrSkunk/theys-gone/commit/49944c54f1997cf3b5f750019b2d0f0023b9204c))


### Performance

* cache VirtualQuery results and GNames misses; hide the ride prompt instead of respawning it ([#15](https://github.com/DrSkunk/theys-gone/issues/15)) ([1e7607f](https://github.com/DrSkunk/theys-gone/commit/1e7607fdc67e5b7d3e9a1862ab3d16eb747e495b))
* **coop:** stop scanning for the controller every tick without one ([#17](https://github.com/DrSkunk/theys-gone/issues/17)) ([8bc7935](https://github.com/DrSkunk/theys-gone/commit/8bc7935c2d6f582c51ec5320c8083336b44136b1))


### Documentation

* clarify co-op roadmap ([#22](https://github.com/DrSkunk/theys-gone/issues/22)) ([64579e5](https://github.com/DrSkunk/theys-gone/commit/64579e58dbdf886b9c5a66180e3e50d07d6531cf))

## [0.5.1](https://github.com/DrSkunk/theys-gone/compare/v0.5.0...v0.5.1) (2026-10-27)


### Documentation

* document objective sync, protocol upgrades and the branding pak ([4d9ef8c](https://github.com/DrSkunk/theys-gone/commit/4d9ef8cf0e00104a8fcad7176b095bf4ef5c500d))

## [0.5.0](https://github.com/DrSkunk/theys-gone/compare/v0.4.0...v0.5.0) (2026-10-27)


### ⚠ BREAKING CHANGES

* the protocol version is now 9; both players must update.

### Features

* free roam with gradual enemy ownership handoff (protocol v9) ([dd32bac](https://github.com/DrSkunk/theys-gone/commit/dd32bac8e61ae411f5886dc5a5aff2e308185c62))
* **research:** engine replication experiment over SteamNetDriver ([d901d94](https://github.com/DrSkunk/theys-gone/commit/d901d948e32614cf7afe6be125ea206d114754bf))
* **research:** late-adoption menu rebuild, game remote tooling, QA notes ([d6782c3](https://github.com/DrSkunk/theys-gone/commit/d6782c3f62dbc85e1d33984a41098b8409c9ab12))
* **research:** live mission lab to observe and drive job transitions ([4573be9](https://github.com/DrSkunk/theys-gone/commit/4573be933cf316ed44ec0a90eee679888eade45b))
* ship Theys Gone title and main-menu branding pak ([0ce7d0e](https://github.com/DrSkunk/theys-gone/commit/0ce7d0eeff34d325f352aeb8eeaf636ecfcc8b97))
* **story:** sync persisted objective progress with sequenced packets ([1a0b15a](https://github.com/DrSkunk/theys-gone/commit/1a0b15a456105dc487283a100da57b4d2a1c5449))


### Bug Fixes

* **overlay:** stop Present recursion when other overlays hook the swap chain ([1d98857](https://github.com/DrSkunk/theys-gone/commit/1d988576d8ff9daa91471107ee220ce6d01d4e9f))


### Documentation

* add AGENTS.md with project state, commands and contributor rules ([3ad5997](https://github.com/DrSkunk/theys-gone/commit/3ad59979d465bcbeae655a4abbc0cf13b811de57))

## [0.4.0](https://github.com/DrSkunk/theys-gone/compare/v0.3.0...v0.4.0) (2026-10-21)


### ⚠ BREAKING CHANGES

* GuestSaveGuard and ShareSave are removed; use HostSave.

### Features

* host-first save and story sync, native ride prompt, research tools ([16773d9](https://github.com/DrSkunk/theys-gone/commit/16773d9ce79e254a0b7ee272aaf03524e4d882b3))
* share host enemies with the guest, both ways ([f999df7](https://github.com/DrSkunk/theys-gone/commit/f999df74a2b0d48b3ae5e1024ca9e0b63c30a086))

## [0.3.0](https://github.com/DrSkunk/theys-gone/compare/v0.2.0...v0.3.0) (2026-10-21)


### ⚠ BREAKING CHANGES

* the plugin is now TheysGone.asi with TheysGone.ini and TheysGone.log; the old DaysGoneTogether.ini is not read. The LAN beacon magic is now "TGGB" and the Steam lobby key is "tgg", so older builds cannot find or join Theys Gone sessions.
* cut pose bandwidth with pose format v2 (protocol v6)

### Features

* cut pose bandwidth with pose format v2 (protocol v6) ([43519d1](https://github.com/DrSkunk/theys-gone/commit/43519d1bbfd8179bdc9a9277de173d4aa9231fcc))
* experimental host-to-guest save transfer ([2ca4b59](https://github.com/DrSkunk/theys-gone/commit/2ca4b59f0e61ae423e1dc78c2491f817dba37a03))
* experimental Steam lobby and networking transport ([0fb4904](https://github.com/DrSkunk/theys-gone/commit/0fb4904cebf2653fd841c068c223584a8d55da32))
* mission/save discovery trace and experimental guest save sandbox ([5ee270d](https://github.com/DrSkunk/theys-gone/commit/5ee270d6e3f7b33b4ae06820fee1648c18ee4d52))
* native CO-OP title/pause menus with Steam and LAN sessions ([febbf9a](https://github.com/DrSkunk/theys-gone/commit/febbf9a772014a4711295ca39ab2aa30c2b30521))
* rename the mod to Theys Gone ([d4eb330](https://github.com/DrSkunk/theys-gone/commit/d4eb33042cef5e6c12995f2c1559e0ffd78edfe0))


### Bug Fixes

* add .DS_Store to .gitignore ([b7dd821](https://github.com/DrSkunk/theys-gone/commit/b7dd821e8ed2174f14136e35032c438b0c94e649))
* clean CO-OP session events and HUD log placement ([84325b1](https://github.com/DrSkunk/theys-gone/commit/84325b1ab8a9ee240c0f4d159a604bc17e8e5ebe))


### Documentation

* sync Nexus copy and tested transport status ([3a96fef](https://github.com/DrSkunk/theys-gone/commit/3a96fef47781272835a8b01a019c39f41d12e601))

## [0.2.0](https://github.com/DrSkunk/days-gone-together/compare/v0.1.0...v0.2.0) (2026-10-19)


### ⚠ BREAKING CHANGES

* the files are now DaysGoneTogether.asi and DaysGoneTogether.ini (log: DaysGoneTogether.log). Delete the old DaysGoneCoop.* files from BendGame/Binaries/Win64 and copy your settings into the new INI.

### Features

* rename the mod to Days Gone Together ([b504bcb](https://github.com/DrSkunk/days-gone-together/commit/b504bcbfb7e9b70f9565ea01b5efbcb749005514))

## 0.1.0 (2026-10-19)


### Features

* embed the release version in the plugin and package release archives ([03f9a70](https://github.com/DrSkunk/days-gone-together/commit/03f9a700dd0e4a7ca9c9983efaf418de05993567))


### Documentation

* rewrite documentation as a public guide and add MIT license ([1a9135d](https://github.com/DrSkunk/days-gone-together/commit/1a9135d0080b0c4002ca45a337abbe095d9bd073))
