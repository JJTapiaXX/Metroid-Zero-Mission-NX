# Screenshots / Capturas de Pantalla

Esta carpeta contiene las imágenes de muestra utilizadas en el `README.md` del proyecto.

## Archivos y Resoluciones

| Archivo | Pantalla | Resolución | Descripción |
|---|---|---|---|
| `preview.png` | Dual Screen | 400x500 | Imagen combinada vertical (pantalla superior + inferior) para la cabecera. |
| `top_gameplay_wide.png` | Superior | 400x240 | Gameplay en modo panorámico (WIDE / 16:9). |
| `top_gameplay_bezel.png` | Superior | 400x240 | Modo Pixel Perfect con marco retro GBA y rejilla LCD. |
| `bottom_map.png` | Inferior | 320x240 | Pestaña **MAP**: mapa en tiempo real, zoom y contadores de items. |
| `bottom_status.png` | Inferior | 320x240 | Pestaña **STATUS**: equipamiento, porcentaje de mapa y trajes. |
| `bottom_states.png` | Inferior | 320x240 | Pestaña **STATE**: lista dinámica de save states con fecha y miniaturas. |
| `bottom_achievements.png` | Inferior | 320x240 | Pestaña **ACHIEVEMENTS**: lista de logros de RetroAchievements e insignias. |
| `bottom_options.png` | Inferior | 320x240 | Pestaña **OPTIONS**: menú de visualización, controles y actualizaciones. |

---

## Cómo añadir tus capturas reales

### Método 1: Captura directa desde Luma3DS
1. En tu consola 3DS, abre el menú Rosalina pulsando `L + Abajo + Select`.
2. Selecciona **Take screenshot**.
3. Las imágenes se guardarán en la tarjeta SD dentro de `/luma/screenshots/`.
4. Copia esas imágenes a tu ordenador, renómbralas con los nombres de la tabla superior y colócalas en esta carpeta sustituyendo los archivos existentes.
5. Ejecuta:
   ```sh
   python3 tools/process_screenshots.py --composite
   ```
   para regenerar automáticamente la imagen de vista previa dual (`preview.png`).

### Método 2: DUMP SCREEN desde el menú DEBUG del juego
1. En una versión compilada con `DEBUG_TOOLS=1`, accede a la pestaña **DEBUG** en la pantalla táctil y pulsa **HERRAMIENTAS / DEBUG TOOLS -> DUMP SCREEN**.
2. Se generarán volcados `.rgb` en la SD en `sdmc:/3ds/Metroid Zero Mission 3DS/debug/`.
3. Pasa esa carpeta a tu PC y conviértelos automáticamente ejecutando:
   ```sh
   python3 tools/process_screenshots.py --import-dir /ruta/a/carpeta/debug/
   ```
   El conversor rotará la orientación nativa de 3DS (portrait a landscape) y corregirá los canales de color BGR8 a RGB8 de forma automática.
