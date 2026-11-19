/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Separate console feed and executable identity; share verified installation. */
#define UPDATER_SWITCH2_FEED 1
#define UPDATER_ASSET "mzmnx-switch2.nro"
#define UPDATER_TITLE "Metroid: Zero Mission NX Switch 2"
#define UPDATER_PATH "sdmc:/switch/mzm-switch2/mzmnx-switch2.nro"
#define UPDATER_FEED "https://api.github.com/repos/JJTapiaXX/Metroid-Zero-Mission-NX/releases?per_page=100"
#include "../../switch/source/updater_switch.c"
