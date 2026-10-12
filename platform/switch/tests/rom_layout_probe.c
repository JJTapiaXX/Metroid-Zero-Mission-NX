/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stddef.h>
#include <string.h>
#include "structs/audio.h"
#include "structs/room.h"

/* Symbol sizes expose the ABI without running either console architecture. */
char probe_pointer_size[sizeof(void *)];
char probe_sound_entry_size[sizeof(struct SoundEntry)];
char probe_sound_track_offset[offsetof(struct SoundEntry, trackGroundNumber)];
char probe_room_entry_rom_size[sizeof(struct RoomEntryRom)];
char probe_room_bg0_offset[offsetof(struct RoomEntryRom, pBg0Data)];
char probe_tileset_entry_size[sizeof(struct TilesetEntry)];
