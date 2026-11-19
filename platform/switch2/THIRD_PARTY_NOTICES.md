# Switch 2 backend dependencies

This backend statically links [libounce](https://github.com/ouncebrew/libounce) and [OunceVK/Mesa](https://github.com/ouncebrew/ouncevk). It builds with devkitPro and uses the packaging/layout documented by [ounce-devkit](https://github.com/ouncebrew/ounce-devkit). The upstream Vulkan clear example was consulted for the `VK_NN_vi_surface` integration. SDK licenses are reproduced under `licenses/`; the main project remains GPL-3.0-or-later.

The test SDK is pinned to the upstream `devkit-20261117` archive. Its SHA-256 was verified against the upstream checksum before extraction. The source revisions reported by that SDK are:

- libounce: `39406be1f25a`
- OunceVK: `114e9298fc0e`
- Kaihen: `ddbf02006672`

The app uses libounce's normal homebrew runtime and OunceVK's Vulkan API. It does not load a custom Kaihen plugin or request JIT access. Kaihen itself is required on the console as the homebrew environment; `libkaihen` is not linked because this game does not need its plugin/JIT API.

Corresponding SDK source is available from the linked public repositories. The original game/port credits and third-party licenses are in the repository's `CREDITS.md` and `THIRD-PARTY-LICENSES.md`.
