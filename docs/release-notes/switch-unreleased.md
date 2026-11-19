# Metroid: Zero Mission NX — Switch 1.4

Version 1.4 for the original Nintendo Switch.

## Second-screen improvements

- New optional **WORLD** view: a connected atlas containing the seven native area maps, with the game's elevator connections and a schematic Crateria–Chozodia surface link. Areas retain their native grids; this is a readable connected atlas, not a claim of exact geographic stitching. Only visited/downloaded area information is shown.
- Touch **WORLD >** on the Map header to enter it, **FIT WORLD** to fit the whole atlas, and **< AREA** to return to the original area map. Zoom, touch panning, follow-Samus, the player marker, and objective markers work in the new view. Area overview remains available at the smallest area zoom.
- The Map header now shows collectible counts. Tap it, or **ITEMS / AREA MAPS** on Status, to open a per-area Energy/Missile/Super Missile/Power Bomb breakdown with map-download indicators and a hide-totals option.

## Credit for the 3DS UI

**tinaut1986** authored the original 3DS second-screen UI that inspired and supplied the reference model for the full-area overview, collectibles breakdown, item percentages, and downloaded-map indicators. Reference source: [port_bottom_ui_3ds.c at bd315e94](https://github.com/tinaut1986/mzm/blob/bd315e949ba5ca33fbc3a483d715f75ef3d941c6/platform/3ds/source/port_bottom_ui_3ds.c), including `GetAreaStats`, `GetGlobalItemStats`, `RenderCollectiblesModal`, and the Map/Status views. The screenshots show a complete single-area map; the multi-area connected atlas is new Switch-side work based on the original game's map/elevator data. Switch adaptation and hardware testing: **JJTapia_XX**. Original authorship and GPL notices are retained in the source and packages; see CREDITS.md and THIRD-PARTY-LICENSES.md.

## Testing and updating

Automated native map/UI checks passed. The earlier full-game playthrough applies to 1.3 development; this update does not claim another complete playthrough. Continue from normal in-game saves when changing builds; save states still require their original executable and ROM. The saved-game format is unchanged by this UI update.

## Audio default

New installations default to 100% master volume. Existing saved volume settings are preserved.
