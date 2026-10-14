#ifndef PORT_PSG_SYNTH_H
#define PORT_PSG_SYNTH_H
#include <stdbool.h>

/*
 * Software synthesis of the GBA's four PSG voices for the 3DS port.
 *
 * On real hardware the mzm engine only software-mixes its Direct Sound
 * voices (into gMusicInfo.soundRawData, which port_mzm_audio_glue.c captures
 * and feeds to NDSP). The other half of the music -- two square channels, the
 * 32-step wave channel and the noise channel -- is produced by the GBA sound
 * chip itself: the engine merely writes its registers (0x04000060-0x0400009F,
 * via unk_5104/UploadSampleToWaveRam) and hardware does the rest.
 *
 * This port has no such hardware. Those register writes land in gIoMem and
 * nothing ever read them back, so every music track assigned to a PSG voice
 * was silent -- see docs/3ds-port-status-2026-08-17.md section 6t, where the
 * user identified the symptom as "some tracks play and others are omitted".
 * This module closes that gap by reading the same emulated registers and
 * synthesising the four voices, which the glue then mixes into the same
 * stereo stream as the Direct Sound PCM.
 *
 * Usage per block of samples (see port_mzm_audio_glue.c):
 *   PortPsg_BeginBlock(rate);            // latch register state once
 *   for (i = 0; i < n; ++i) {
 *       PortPsg_NextSample(&l, &r);      // advance and emit one frame
 *   }
 * Register state is only re-read on BeginBlock, matching the engine's own
 * cadence of updating the PSG registers once per audio frame.
 */

/* Latch the current PSG register state from gIoMem and recompute per-sample
 * increments for the given output sample rate. Detects note retriggers (the
 * engine sets bit 15 of each channel's frequency register on note-on, then
 * clears it in its own copy, so a retrigger shows up as a changed value with
 * that bit set). */
void PortPsg_BeginBlock(unsigned int rate);

/* Emit one stereo frame, advancing all four voices. Output is centred on 0
 * and roughly matches the amplitude scale of the engine's Direct Sound PCM
 * once shifted to 16-bit (see PSG_UNIT_SCALE in the implementation). */
void PortPsg_NextSample(int* outLeft, int* outRight);
void PortPsg_NextSampleGain(int* outLeft,int* outRight,const unsigned gains[4]);

/* Returns true if any PSG voice is currently producing sound, used to manage
 * stream lifecycle. */
bool PortPsg_IsAnyVoiceActive(void);

/* Clear all synthesis state (phases, envelopes, noise LFSR). Called from the
 * glue's init so a fresh run never starts mid-note. */
void PortPsg_Reset(void);

#endif /* PORT_PSG_SYNTH_H */
