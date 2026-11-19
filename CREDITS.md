# Credits and provenance

## Switch adaptation

**JJTapia_XX** — Switch adaptation maintainer, feature direction, hardware testing, and release packaging. Displayed as the NRO author at the maintainer's request.

## Upstream authors and contributors

- [tinaut1986/mzm](https://github.com/tinaut1986/mzm): 3DS port and shared native-port infrastructure. This project is derived from that repository; its history and notices are retained.
- [metroidret/mzm](https://github.com/metroidret/mzm): Metroid: Zero Mission decompilation, including YohannDR and the contributors recorded in upstream history.
- [EstebanPdN/zelda-tmc-3ds](https://github.com/EstebanPdN/zelda-tmc-3ds): platform and 3DS frontend foundation credited by upstream.
- [Mathéo Vignaud / VirtuaPPU](https://github.com/MatheoVignaud/VirtuaPPU): software rendering foundation.
- [devkitPro](https://devkitpro.org/) and [switchbrew/libnx](https://github.com/switchbrew/libnx): homebrew toolchain and Switch platform library.
- [RetroAchievements](https://retroachievements.org/) community and [rcheevos](https://github.com/RetroAchievements/rcheevos): achievement set, service, runtime, and parsers.
- [stb](https://github.com/nothings/stb) contributors, including Sean Barrett: PNG image decoder.

This is not a claim that the Switch maintainer wrote all underlying game or library code. Individual file notices, component licenses, and Git history remain the source of detailed authorship information.

## Second-screen UI provenance

**tinaut1986** is the original author of the 3DS second-screen UI used as the reference for the complete area-map overview, per-area collectible breakdown, item percentages, and downloaded-map indicators adapted for the Switch frontends. Reference: [`port_bottom_ui_3ds.c` at `bd315e949ba5ca33fbc3a483d715f75ef3d941c6`](https://github.com/tinaut1986/mzm/blob/bd315e949ba5ca33fbc3a483d715f75ef3d941c6/platform/3ds/source/port_bottom_ui_3ds.c), including `GetAreaStats`, `GetGlobalItemStats`, `RenderCollectiblesModal`, and the Map/Status views. See also the upstream [map screenshot](https://github.com/tinaut1986/mzm/blob/bd315e949ba5ca33fbc3a483d715f75ef3d941c6/screenshots/bottom_map.png) and [status screenshot](https://github.com/tinaut1986/mzm/blob/bd315e949ba5ca33fbc3a483d715f75ef3d941c6/screenshots/bottom_status.png).

The Switch connected-world atlas, touch controls, CPU rasterization, and platform integration are new Switch-side work built on that upstream UI and the original game's map/elevator data. The reference screenshot depicts a complete single-area map; it is not presented here as evidence that the upstream author implemented this new multi-area atlas. Original GPL licensing and upstream notices are retained. Release notes for these changes must preserve this attribution.

## Game and artwork

Metroid: Zero Mission, its characters, original artwork, and trademarks belong to Nintendo and their respective rights holders. Original gameplay/map assets are loaded from the user's ROM. The NRO cover was supplied by the maintainer; the original artwork artist/license is unverified. Bundled achievement badges were inherited from the 3DS port; badge artwork belongs to its respective creators or rights holders and is not automatically covered by the software license. Attribution is not a substitute for permission.

For software license texts and component notices, see [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md) and [LICENSE](LICENSE).
