# Third-Party Licenses and Attributions

This project combines components licensed under different open-source licenses. The complete combined project is distributed under the terms of the **GNU General Public License v3.0 (GPL-3.0-or-later)** as detailed in [LICENSE](LICENSE).

Below are the attributions and original license terms for individual components and upstream projects incorporated into this port:

---

## 1. Upstream Metroid: Zero Mission Decompilation (`metroidret/mzm`)

- **Repository**: <https://github.com/metroidret/mzm>
- **License**: MIT License
- **Notice**:

```text
Copyright 2025 YohannDR

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

---

## 2. Zelda: The Minish Cap 3DS Port (`EstebanPdN/zelda-tmc-3ds`)

- **Repository**: <https://github.com/EstebanPdN/zelda-tmc-3ds>
- **License**: GNU General Public License v3.0 (GPL-3.0)
- **Role**: Provided the platform architecture, Citro2D/Citro3D presentation pipeline, and 3DS frontend implementation foundation.

---

## 3. VirtuaPPU

- **Author**: Mathéo Vignaud
- **Repository**: <https://github.com/MatheoVignaud/VirtuaPPU>
- **License**: GNU General Public License v3.0 or later (GPL-3.0-or-later)
- **Role**: Software GBA Picture Processing Unit (PPU) base located in `port/ppu/`.

---

## 4. devkitPro / libctru / citro2d / citro3d

- **Website**: <https://devkitpro.org/>
- **License**: zlib / 2-Clause BSD / Custom Permissive Homebrew Licenses
- **Role**: Toolchain, Nintendo 3DS hardware abstraction and graphics rendering libraries.

---

## 5. rcheevos (RetroAchievements)

- **Repository**: <https://github.com/RetroAchievements/rcheevos>
- **License**: MIT License
- **Role**: RetroAchievements runtime library and parser.

---

## 6. stb_image (`nothings/stb`)

- **Repository**: <https://github.com/nothings/stb>
- **License**: MIT or Public Domain (dual-licensed; see the end of `third_party/stb/stb_image.h`)
- **Role**: PNG decoding of RetroAchievements badges downloaded at runtime (`platform/3ds/source/port_retroachievements_3ds.c` and `platform/switch/source/achievement_badges.c`).

## 7. libnx (Switch platform library)

- **Repository**: <https://github.com/switchbrew/libnx>
- **License**: ISC-style permissive license
- **Role**: Switch services, input, framebuffer, audio, sockets, and TLS.

```text
Copyright 2017-2018 libnx Authors

Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby granted, provided that the above copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
```

## 8. rcheevos license text

The complete license is also preserved at `third_party/rcheevos/LICENSE`.

```text
MIT License

Copyright (c) 2018 RetroAchievements.org

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

```

## Artwork scope

Software license notices do not grant rights to Nintendo game assets, trademarks, the supplied cover art, or independently authored achievement badges. See `CREDITS.md` and the README for provenance and scope. No ROM is included.

## 9. stb_image MIT notice

```text
Copyright (c) 2017 Sean Barrett
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
