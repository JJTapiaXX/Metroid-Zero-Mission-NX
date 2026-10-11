#---------------------------------------------------------------------------------
# Metroid Zero Mission - 3DS Port Makefile
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
  DEVKITPRO := /opt/devkitpro
endif
ifeq ($(strip $(DEVKITARM)),)
  DEVKITARM := $(DEVKITPRO)/devkitARM
endif

CTRULIB   := $(DEVKITPRO)/libctru
PORTLIBS  := $(DEVKITPRO)/portlibs/3ds

CC        := $(DEVKITARM)/bin/arm-none-eabi-gcc
CXX       := $(DEVKITARM)/bin/arm-none-eabi-g++
LD        := $(DEVKITARM)/bin/arm-none-eabi-gcc
NM        := $(DEVKITARM)/bin/arm-none-eabi-nm
OBJCOPY   := $(DEVKITARM)/bin/arm-none-eabi-objcopy
READELF   := $(DEVKITARM)/bin/arm-none-eabi-readelf
SMDHTOOL  := $(DEVKITPRO)/tools/bin/smdhtool
3DSXTOOL  := $(DEVKITPRO)/tools/bin/3dsxtool

ROOT      := $(abspath $(CURDIR)/../..)
BUILD     := $(CURDIR)/build
TARGET    := $(CURDIR)/mzm-3ds

# Give the compiler somewhere to put its temporary files.
#
# On Windows the make that ships with devkitPro is a Cygwin build, and it does
# not pass TMP/TEMP through to recipes -- they arrive empty. devkitARM's gcc is
# a native Windows binary, so with neither set it falls back to what
# GetTempPath() returns, which is C:\WINDOWS, and every compile dies with
# "Cannot create temporary file in C:\WINDOWS\: Permission denied". Pointing
# them at the build directory fixes it there and changes nothing on Linux or
# macOS, where the variables are normally unset and gcc would use /tmp.
ifeq ($(strip $(TMP)),)
  export TMP  := $(BUILD)/tmp
  export TEMP := $(BUILD)/tmp
  $(shell mkdir -p $(BUILD)/tmp)
endif

APP_TITLE       := Metroid Zero Mission 3DS
# Recursively expanded (=) so it's resolved at recipe time, once VERSION is known.
# SMDH text fields have a hard 64-character limit, so the version is truncated
# if "Native 3DS port of Metroid Zero Mission <VERSION>" would overflow it.
APP_DESCRIPTION  = $(shell printf '%s %s' "Native 3DS port of Metroid Zero Mission" "$(VERSION)" | cut -c1-64)
APP_AUTHOR      := metroidret + community
APP_ICON        := $(CURDIR)/assets/icon-48.png
ifeq ($(wildcard $(APP_ICON)),)
  APP_ICON := $(CTRULIB)/default_icon.png
endif

# Fuentes C a compilar (relativas a ROOT)
SRCS_ROOT := \
	$(wildcard $(ROOT)/src/*.c) \
	$(wildcard $(ROOT)/src/cutscenes/*.c) \
	$(wildcard $(ROOT)/src/menus/*.c) \
	$(wildcard $(ROOT)/src/sprites_ai/*.c) \
	$(wildcard $(ROOT)/src/sram/*.c) \
	$(ROOT)/src/data/shortcut_pointers.c \
	$(ROOT)/src/data/music_track_data.c \
	$(ROOT)/src/data/audio.c \
	$(ROOT)/src/data/color_fading_data.c \
	$(ROOT)/src/data/cutscenes/cutscenes_data.c \
	$(ROOT)/src/data/in_game_cutscene_data.c \
	$(ROOT)/src/data/empty_datatypes.c \
	$(ROOT)/port/port_gba_symbols.c \
	$(ROOT)/port/port_rom.c \
	$(ROOT)/port/port_gba_mem.c \
	$(ROOT)/port/port_bios.c \
	$(ROOT)/port/port_gba_timing.c \
	$(ROOT)/port/port_debug_log.c \
	$(ROOT)/port/port_debug_files.c \
	$(ROOT)/port/port_constructor_init.c \
	$(ROOT)/port/port_mzm_audio_glue.c \
	$(ROOT)/port/port_psg_synth.c \
	$(ROOT)/port/ppu/src/virtuappu.c \
	$(ROOT)/port/ppu/src/mode1.c \
	$(filter-out %_selftest.c %_test.c, $(wildcard $(ROOT)/port/generated/*.c))



# Motor de audio real de mzm: UpdateAudio/InitTrack/UpdateTrack viven en
# asm/audio_internal.s (ARM Thumb sin descompilar) y el mezclador PCM en
# asm/soundcode.s. Ya incluyen el fix de port_resolve_addr() en InitTrack.
# Se enlazan en el build 3DS para que el sonido use el engine propio de mzm;
# el PCM se consume desde gMusicInfo.soundRawData por el pegamento NDSP
# (port_mzm_audio_3ds.c).
SRCS_ASM := \
	$(ROOT)/asm/intr_main.s \
	$(ROOT)/asm/linker_symbols.s \
	$(ROOT)/asm/audio_internal.s \
	$(ROOT)/asm/soundcode.s

SRCS_3DS := \
	$(CURDIR)/source/main_3ds.c \
	$(CURDIR)/source/platform_3ds_minimal.c \
	$(CURDIR)/source/port_oam_census.c \
	$(CURDIR)/source/platform_gpu_3ds.c \
	$(CURDIR)/source/port_bottom_ui_3ds.c \
	$(CURDIR)/source/port_ppu_mzm.c \
	$(CURDIR)/source/port_gpu_renderer.c \
	$(CURDIR)/source/port_save_state.c \
	$(CURDIR)/source/port_stereo_depth.c \
	$(CURDIR)/source/port_cutscene_depth.c \
	$(CURDIR)/source/port_layer_fixes.c \
	$(CURDIR)/source/port_hud_oam.c \
	$(CURDIR)/source/port_overlay_text_oam.c \
	$(CURDIR)/source/port_sprite_depth_oam.c \
	$(CURDIR)/source/port_mzm_audio_3ds.c \
	$(CURDIR)/source/port_retroachievements_3ds.c \
	$(CURDIR)/source/port_updater_3ds.c \
	$(CURDIR)/source/port_updater_parse.c \
	$(CURDIR)/source/net_3ds.c \
	$(CURDIR)/source/ftp_server_3ds.c \
	$(CURDIR)/source/cia_install_3ds.c \
	$(CURDIR)/source/debug_link_3ds.c \
	$(CURDIR)/source/port_ra_iwram_map.c \
	$(CURDIR)/source/port_ra_badges_data.c \
	$(CURDIR)/source/port_ra_unlock_sound_data.c \
	$(CURDIR)/source/port_haze_3ds.c \
	$(CURDIR)/source/port_gba_screen_fx.c \
	$(CURDIR)/source/port_gba_bezel.c \
	$(CURDIR)/source/port_gba_bezel_data.c \
	$(CURDIR)/source/port_state_thumb.c \
	$(CURDIR)/source/port_state_blob.c \
	$(CURDIR)/source/port_state_vars.c \
	$(CURDIR)/source/port_state_ptrs.c \
	$(CURDIR)/source/port_wide_view.c

# rcheevos: the official RetroAchievements library, vendored under
# third_party/rcheevos (see its VERSION.txt for the commit, and for which of
# its sources are deliberately left out).
RCHEEVOS := $(ROOT)/third_party/rcheevos
SRCS_RCHEEVOS := \
	$(wildcard $(RCHEEVOS)/src/*.c) \
	$(wildcard $(RCHEEVOS)/src/rapi/*.c) \
	$(wildcard $(RCHEEVOS)/src/rcheevos/*.c) \
	$(RCHEEVOS)/src/rhash/md5.c

# Generar objetos
OBJS_ROOT := $(patsubst $(ROOT)/%.c, $(BUILD)/root/%.o, $(SRCS_ROOT))
OBJS_ASM  := $(patsubst $(ROOT)/%.s, $(BUILD)/root/%.o, $(SRCS_ASM))
OBJS_3DS  := $(patsubst $(CURDIR)/%.c, $(BUILD)/3ds/%.o, $(SRCS_3DS))
OBJS_RC   := $(patsubst $(RCHEEVOS)/%.c, $(BUILD)/rcheevos/%.o, $(SRCS_RCHEEVOS))

# PICA200 vertex shaders: picasso assembles each into a .shbin, bin2s turns
# that into an object plus a header (<name>_shbin / <name>_shbin_size) the C
# side includes.
SHADERS     := $(wildcard $(CURDIR)/source/*.v.pica)
SHADER_OBJS := $(patsubst $(CURDIR)/source/%.v.pica, $(BUILD)/shaders/%.shbin.o, $(SHADERS))
PICASSO     := $(DEVKITPRO)/tools/bin/picasso
BIN2S       := $(DEVKITPRO)/tools/bin/bin2s

C_OBJS    := $(OBJS_ROOT) $(OBJS_ASM) $(OBJS_3DS) $(OBJS_RC) $(SHADER_OBJS)
DEPS      := $(C_OBJS:.o=.d)

# Asegurar que existan todos los directorios de salida y tmp antes de compilar en paralelo
$(shell mkdir -p $(BUILD)/tmp $(sort $(dir $(C_OBJS))))

GIT_BRANCH  := $(shell git rev-parse --abbrev-ref HEAD 2>/dev/null)
EXACT_TAG   := $(shell git describe --tags --exact-match 2>/dev/null)
# Rama release/* más reciente que contiene HEAD (local u origin): permite
# que ramas laterales (p.ej. PRs basados en release) hereden su versión
REL_MERGED := $(shell git for-each-ref --format='%(refname:short)' --merged HEAD refs/heads/'release/*' refs/remotes/'origin/release/*' 2>/dev/null | sort -V | tail -1)
REL_BASE    := $(shell git rev-parse --verify -q refs/remotes/origin/main >/dev/null && echo refs/remotes/origin/main || echo main)

ifneq ($(strip $(EXACT_TAG)),)
  GIT_VERSION := $(EXACT_TAG)
else
ifeq ($(filter release/%,$(GIT_BRANCH)),)
  REL_NAME    := $(subst origin/,,$(REL_MERGED))
else
  REL_NAME    := $(GIT_BRANCH)
endif
ifneq ($(filter release/%,$(REL_NAME)),)
  REL_TARGET  := $(shell git rev-parse --verify -q refs/heads/$(REL_NAME) >/dev/null && echo refs/heads/$(REL_NAME) || echo refs/remotes/origin/$(REL_NAME))
  REL_CNT     := $(shell git rev-list --count $(REL_BASE)..$(REL_TARGET) 2>/dev/null || echo 1)
  DEV_CNT     := $(shell git rev-list --count $(REL_TARGET)..HEAD 2>/dev/null || echo 0)
  REL_HASH    := $(shell git rev-parse --short HEAD 2>/dev/null || echo dev)
  ifeq ($(DEV_CNT),0)
    GIT_VERSION := $(patsubst release/%,%,$(REL_NAME))-dev.$(REL_CNT)+$(REL_HASH)
  else
    GIT_VERSION := $(patsubst release/%,%,$(REL_NAME))-dev.$(REL_CNT).$(DEV_CNT)+$(REL_HASH)
  endif
else
  GIT_VERSION := $(shell git describe --tags --always 2>/dev/null || echo "0.0-dev")
endif
endif
VERSION ?= $(GIT_VERSION)

# CHANNEL=beta marks the build as a beta (the console shows BETA, and the
# updater offers the stable build of the same X.Y.Z once it exists). Only the CI
# sets it, from whether main reaches the tagged commit; local builds are neither.
# Like DEBUG_TOOLS it does not invalidate objects: `make clean` when changing it.
CHANNEL ?=

# Which retail region's code paths to compile in.
#
# REGION=any (the default) defines no REGION_* macro at all: every region's
# behaviour is compiled and selected at runtime from gRomRegion (see
# include/region.h), so ONE binary works with an EUR, USA or JAP ROM.
#
# eu/us/jp remain for bisecting a region-specific problem: they pin the
# decomp's compile-time `#if REGION_*` choices to that region, which also
# restricts the binary to that region's ROM.
REGION  ?= any
ifeq ($(REGION),any)
  REGION_DEFINE :=
else ifeq ($(REGION),eu)
  REGION_DEFINE := -DREGION_EU
else ifeq ($(REGION),us)
  REGION_DEFINE := -DREGION_US
else ifeq ($(REGION),jp)
  REGION_DEFINE := -DREGION_JP
else
  $(error REGION must be one of: any eu us jp)
endif

ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS  := -O2 -fomit-frame-pointer -Wall -mword-relocations -ffunction-sections -fdata-sections \
           $(REGION_DEFINE) -DMZM_PORT_VERSION=\"$(VERSION)\" $(if $(filter beta,$(CHANNEL)),-DMZM_PORT_BETA=1) -D__3DS__ -DMZM_3DS \
           -DNON_MATCHING=1 \
           -Wno-implicit-function-declaration -Wno-implicit-int -Wno-builtin-declaration-mismatch \
           -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function \
           -Wno-return-mismatch -Wno-stringop-overflow -Wno-strict-aliasing -Wno-parentheses \
           -Wno-int-conversion -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast \
           $(ARCH)


# Which top-screen renderer to build: gpu (default) offloads mode-0 tile/
# sprite compositing to the PICA200 (port_gpu_renderer.c), falling back to
# the CPU scanline renderer per-frame for anything it doesn't support yet
# (affine BG, blend, windows, mosaic, affine OBJ -- see
# Port_GpuRenderer_CanRenderFrame). cpu forces the plain CPU scanline path
# (mode1.c) unconditionally, e.g. to isolate whether a bug is renderer-
# specific:
#   make clean && make RENDERER=cpu
# The clean matters: changing flags alone does not invalidate existing
# objects, which is exactly the stale-.o trap documented in
# docs/3ds-port-status-2026-08-17.md section 6l.
RENDERER ?= gpu
ifeq ($(RENDERER),gpu)
  CFLAGS += -DPORT_GPU_TILE_RENDERER
else ifneq ($(RENDERER),cpu)
  $(error Unknown RENDERER '$(RENDERER)' -- expected 'gpu' or 'cpu')
endif

# Force the Old3DS performance profile even when actually running on New3DS
# hardware: no CPU/L2 clock speedup (osSetSpeedupEnable is skipped, so the
# CPU stays at Old3DS's base clock instead of jumping to New3DS's 804MHz),
# no extra Core1 worker thread (APT_SetAppCpuTimeLimit is skipped), and the
# PPU/GPU renderers pick their Old3DS render profile (adaptive frameskip
# enabled, virtuappu_mode1's old3ds mode1 profile) -- see the sIsNew3DS
# override in Platform3DS_Init (platform_3ds_minimal.c). Lets you benchmark
# how the game would actually run on an Old3DS/2DS by building and
# installing this on a New3DS you already own, no separate console needed:
#   make clean && make FORCE_OLD3DS=1
# The clean matters: changing flags alone does not invalidate existing
# objects, which is exactly the stale-.o trap documented in section 6l.
FORCE_OLD3DS ?= 0
ifeq ($(FORCE_OLD3DS),1)
  CFLAGS += -DPORT_FORCE_OLD3DS_PROFILE
else ifneq ($(FORCE_OLD3DS),0)
  $(error Unknown FORCE_OLD3DS '$(FORCE_OLD3DS)' -- expected '0' or '1')
endif

# THE debug build. There is exactly one flag:
#   make clean && make DEBUG_TOOLS=1
# It compiles in the bottom screen's DEBUG -> HERRAMIENTAS menu and every
# tool behind it (instant-kill, scene recorder, perf recorder, live atlas
# dump, screen/state dump, USER MARK log line, warp, equipment -- see
# docs/3ds-debug-tools.md) AND every throttled per-frame diagnostic log
# stream (GPU, audio, PERF). All of it is source-level absent from a
# production build, so that build has NO WAY to trigger any of it.
#
# There used to be three opt-in flags -- PORT_DEBUG_TOOLS for the menu,
# PORT_GPU_RENDERER_DIAG_LOG / PORT_AUDIO_DIAG_LOG (and an ad-hoc
# PORT_PPU_PERF_LOG) for the logs, each its own EXTRA_CFLAGS. Now a
# DEBUG_TOOLS=1 build compiles every stream in and you pick which one(s)
# write from the menu: LOG mode OFF / ALL / GPU / AUDIO / PERF (nothing hits
# <game folder>/debug/mzm-debug.log while it is OFF -- port_debug_log.h). So a debug
# build that is never asked to log costs nothing, and the flag split stopped
# earning its keep.
# The clean matters: changing flags alone does not invalidate existing
# objects, which is exactly the stale-.o trap documented in section 6l.
DEBUG_TOOLS ?= 0
ifeq ($(DEBUG_TOOLS),1)
  CFLAGS += -DPORT_DEBUG_TOOLS
else ifneq ($(DEBUG_TOOLS),0)
  $(error Unknown DEBUG_TOOLS '$(DEBUG_TOOLS)' -- expected '0' or '1')
endif

# Extra flags from the command line, for one-off experiments. The debug
# tooling no longer needs any (see DEBUG_TOOLS above); this stays for things
# like -DPORT_VERBOSE_FRAME_LOG when bisecting a boot hang.
#   make clean && make EXTRA_CFLAGS=-DPORT_VERBOSE_FRAME_LOG
# The clean matters: changing flags alone does not invalidate existing objects,
# which is exactly the stale-.o trap documented in section 6l.
CFLAGS  += $(EXTRA_CFLAGS)

# Warning suppressions that apply ONLY to the decompiled game (src/) and the
# generated ROM shims (port/), NOT to the hand-written 3DS port under
# platform/3ds/source (which compiles clean and must keep compiling clean --
# a warning there is a real signal and should not drown in decomp noise).
# These are all "the decomp does not write idiomatic C, but it byte-matches
# the original" cases: out-of-bounds subscripts the optimiser infers on
# unreachable paths, functions that fall off the end with the return value
# already in r0, i = i++ style sequence points, constant truncations, etc.
# Fixing them upstream-side would mean diverging from the decomp.
CFLAGS_DECOMP := $(CFLAGS) \
    -Wno-array-bounds -Wno-maybe-uninitialized -Wno-overflow \
    -Wno-unused-value -Wno-sequence-point -Wno-return-type \
    -Wno-unused-label -Wno-misleading-indentation -Wno-sizeof-array-div \
    -Wno-pointer-sign -Wno-discarded-qualifiers

INCLUDES := \
	-I$(ROOT)/port/generated/shadow \
	-I$(ROOT) \
	-I$(ROOT)/include \
	-I$(ROOT)/port \
	-I$(ROOT)/port/generated \
	-I$(ROOT)/port/ppu/include \
	-I$(CURDIR)/source \
	-I$(BUILD)/shaders \
	-I$(ROOT)/third_party/rcheevos/include \
	-I$(ROOT)/third_party/rcheevos/src/rhash \
	-I$(ROOT)/third_party/stb \
	-I$(CTRULIB)/include \
	-I$(PORTLIBS)/include

# -B$(CURDIR): makes the -specs=3dsx.specs `-T 3dsx.ld%s` resolve to our
# vendored copy in this directory (3dsx.ld), which adds the save-state section
# brackets. ewram_symbols.ld stays a pure symbol-assignment augmenter.
LDFLAGS  := -specs=3dsx.specs -B$(CURDIR) -g $(ARCH) -Wl,-Map,$(BUILD)/mzm-3ds.map -Wl,--gc-sections -T $(CURDIR)/ewram_symbols.ld
LIBS     := -L$(CTRULIB)/lib -L$(PORTLIBS)/lib -lcitro2d -lcitro3d -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lctru -lm


# The committed tools/bin/{makerom,bannertool} are Linux x86-64 ELF binaries,
# bundled for reproducible CI builds. On Windows they fail with "Exec format
# error" (make Error 126), so there we must skip them and use the .exe that
# ships with devkitPro instead. devkitPro's Windows make is an Msys build and
# scrubs $(OS), so detect via `uname -o` (Msys / Cygwin) rather than $(OS).
ifneq (,$(filter Msys Cygwin,$(shell uname -o 2>/dev/null)))
MAKEROM    ?= $(firstword $(wildcard $(DEVKITPRO)/tools/bin/makerom.exe) $(shell which makerom.exe 2>/dev/null))
BANNERTOOL ?= $(firstword $(wildcard $(DEVKITPRO)/tools/bin/bannertool.exe) $(shell which bannertool.exe 2>/dev/null))
else
MAKEROM    ?= $(firstword $(wildcard $(ROOT)/tools/bin/makerom $(HOME)/tools/bin/makerom $(DEVKITPRO)/tools/bin/makerom $(shell which makerom 2>/dev/null)))
BANNERTOOL ?= $(firstword $(wildcard $(ROOT)/tools/bin/bannertool $(HOME)/tools/bin/bannertool $(DEVKITPRO)/tools/bin/bannertool $(shell which bannertool 2>/dev/null)))
endif

.PHONY: all clean cia elf 3dsx print-version ftp test

# `all` must be the default goal. Keep it as the first real target rule in the
# file: an explicit-target rule placed above it (with no recipe, e.g. an extra
# prerequisite line) would silently become the default goal instead -- that is
# exactly what happened when the port_layer_fixes.o dep line below sat here, so
# a bare `make` built one object and stopped, and CI's `cp mzm-3ds.cia` failed.
.DEFAULT_GOAL := all

# The self-updater needs libcurl + mbedtls (the console's own TLS cannot talk
# to GitHub). Fail early with the fix instead of a wall of linker errors.
.PHONY: check-portlibs
check-portlibs:
	@test -f $(PORTLIBS)/lib/libcurl.a -a -f $(PORTLIBS)/lib/libmbedtls.a || { \
		echo "error: missing 3DS portlibs (libcurl/mbedtls). Install them with:"; \
		echo "  dkp-pacman -S 3ds-curl 3ds-mbedtls 3ds-zlib     (Windows: pacman -S ... in the devkitPro MSYS2 shell)"; \
		exit 1; }

all: check-portlibs $(TARGET).cia

# La lista de correcciones es un include opcional, y cuando port_layer_fixes.c
# se compiló por primera vez todavía no existía: su .d no la menciona y make no
# reconstruye al añadirla. Explícito, entonces. La ruta es la del patrón
# $(BUILD)/3ds/%.o (ver más abajo), no $(BUILD)/%.o.
$(BUILD)/3ds/source/port_layer_fixes.o: $(wildcard $(CURDIR)/source/port_layer_fixes.inc)
$(BUILD)/3ds/source/port_cutscene_depth.o: $(wildcard $(CURDIR)/source/port_cutscene_depth.inc)
# Generated shader header, same reason: it does not exist before the first build.
$(BUILD)/3ds/source/port_gpu_renderer.o: $(BUILD)/shaders/tilequad_shbin.h

$(BUILD)/shaders/%.shbin: $(CURDIR)/source/%.v.pica
	@mkdir -p $(dir $@)
	@echo "PICASSO $(notdir $<)"
	@$(PICASSO) -o $@ $<

$(BUILD)/shaders/%.shbin.o $(BUILD)/shaders/%_shbin.h: $(BUILD)/shaders/%.shbin
	@echo "BIN2S $(notdir $<)"
	@cd $(BUILD)/shaders && $(BIN2S) -a 4 -H $*_shbin.h $*.shbin > $*.shbin.s
	@$(CC) $(ARCH) -x assembler-with-cpp -c $(BUILD)/shaders/$*.shbin.s -o $(BUILD)/shaders/$*.shbin.o

cia: $(TARGET).cia

3dsx: $(TARGET).3dsx

elf: $(TARGET).elf

print-version:
	@echo $(VERSION)

# Host-side tests. No devkitARM, no 3DS, no ROM -- these cover logic that is
# a pure function of GBA register state, which is where every stereo-depth
# bug so far has actually lived. Run them before deploying:
#   make -C platform/3ds test
TEST_CC ?= cc
test: test-state
	@$(TEST_CC) -std=c11 -O1 -Wall -Wextra -Werror -I$(CURDIR)/source \
		-o $(BUILD)/stereo_depth_test \
		$(CURDIR)/tests/stereo_depth_test.c $(CURDIR)/source/port_stereo_depth.c \
		$(CURDIR)/source/port_cutscene_depth.c
	@$(BUILD)/stereo_depth_test
	@$(TEST_CC) -std=c11 -O1 -Wall -Wextra -Werror -I$(CURDIR)/source \
		-o $(BUILD)/cutscene_depth_test \
		$(CURDIR)/tests/cutscene_depth_test.c $(CURDIR)/source/port_cutscene_depth.c
	@$(BUILD)/cutscene_depth_test
	@$(TEST_CC) -std=c11 -O1 -Wall -Wextra -Werror -I$(CURDIR)/source \
		-o $(BUILD)/layer_fixes_test \
		$(CURDIR)/tests/layer_fixes_test.c $(CURDIR)/source/port_layer_fixes.c
	@$(BUILD)/layer_fixes_test
	@$(TEST_CC) -std=c11 -O1 -Wall -Wextra -Werror -I$(CURDIR)/source 		-o $(BUILD)/oam_census_test 		$(CURDIR)/tests/oam_census_test.c $(CURDIR)/source/port_oam_census.c
	@$(BUILD)/oam_census_test
	@$(TEST_CC) -std=c11 -O1 -Wall -Wextra -Werror -I$(CURDIR)/source \
		-o $(BUILD)/updater_parse_test \
		$(CURDIR)/tests/updater_parse_test.c $(CURDIR)/source/port_updater_parse.c
	@$(BUILD)/updater_parse_test

# Save state pointer relocation, on its own so it runs without the rest.
.PHONY: test-state
test-state:
	@# The save state pointer codec stores 32-bit host pointers, so it is tested as a
	@# 32-bit program (needs a 32-bit libc: gcc-multilib).
	@$(TEST_CC) -m32 -std=gnu11 -O1 -Wall -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -DMZM_3DS \
		-I$(ROOT)/include -I$(ROOT)/port -I$(ROOT) -I$(ROOT)/port/generated/shadow -I$(CURDIR)/source \
		-Wl,--defsym,__ss_data_end=__ss_data_start+0x400 -Wl,--defsym,__ss_bss_end=__ss_bss_start+0x400 \
		-o $(BUILD)/state_ptrs_test \
		$(CURDIR)/tests/state_ptrs_test.c $(CURDIR)/source/port_state_ptrs.c $(CURDIR)/source/port_state_vars.c
	@$(BUILD)/state_ptrs_test

# Reference renderer for the GPU-renderer regression harness: renders the
# PPU states in a fetched mzm-rec-NN.bin through the port's own CPU PPU and
# writes them as PPMs. Host-only, no devkitARM, no ROM -- mode1.c takes its
# GBA memory through virtuappu_mode1_bind_gba_memory, so a recording is all
# the input it needs. See platform/3ds/tests/rec_render.c for what the
# comparison is and why the CPU PPU is the oracle.
#
#   make -C platform/3ds rec-render
#   platform/3ds/build/rec_render <mzm-rec-NN.bin> <out-dir> [sample]
rec-render: $(BUILD)/rec_render
#
# -DPORT_NATIVE, not -DMZM_3DS: it selects the same native 240x160 geometry
# the console build uses (see virtuappu.h) without dragging in the 3DS
# worker-thread machinery, so the reference frames come out of the same
# code paths that run on hardware.
$(BUILD)/rec_render: $(CURDIR)/tests/rec_render.c $(ROOT)/port/ppu/src/mode1.c $(ROOT)/port/ppu/src/virtuappu.c
	@mkdir -p $(BUILD)
	@$(TEST_CC) -std=c11 -O2 -Wall -Wextra -Wno-unknown-pragmas -DPORT_NATIVE 		-I$(ROOT)/port/ppu/include -I$(ROOT)/port -I$(ROOT)/include -I$(ROOT) 		-o $@ $^
	@echo "built $@"

ftp: $(TARGET).cia
	@echo "Uploading $(TARGET).cia as mzm-3ds-$(VERSION).cia to FTP..."
	curl --ftp-create-dirs -T $(TARGET).cia "ftp://$(FTP_HOST):$(FTP_PORT)/cias/mzm-3ds-$(VERSION).cia"

$(TARGET).cia: $(TARGET).elf $(BUILD)/mzm-3ds.smdh $(BUILD)/banner.bnr
	@echo "Creating CIA: $(notdir $@)"
	@$(MAKEROM) -f cia -o $@ -elf $(TARGET).elf -rsf $(CURDIR)/cia/mzm3ds.rsf \
		-icon $(BUILD)/mzm-3ds.smdh -banner $(BUILD)/banner.bnr -DAPP_ROMFS=$(CURDIR)/romfs
	@echo "CIA build successful: $@"

$(BUILD)/banner.bnr: $(CURDIR)/assets/banner.png $(CURDIR)/assets/banner-descent-into-the-labyrinth.wav
	@mkdir -p $(dir $@)
	@echo "Creating Banner: $(notdir $@)"
	@$(BANNERTOOL) makebanner -i $(CURDIR)/assets/banner.png -a $(CURDIR)/assets/banner-descent-into-the-labyrinth.wav -o $@

$(BUILD)/mzm-3ds.smdh: $(CURDIR)/assets/icon-48.png
	@mkdir -p $(dir $@)
	@echo "Creating SMDH: $(notdir $@)"
	@$(BANNERTOOL) makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" -i $< -o $@

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh
	@echo "Creating 3DSX: $(notdir $@)"
	@$(3DSXTOOL) $(TARGET).elf $@ --smdh=$(TARGET).smdh

$(TARGET).smdh: $(APP_ICON)
	@echo "Creating SMDH: $(notdir $@)"
	@$(SMDHTOOL) --create "$(APP_TITLE)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" $(APP_ICON) $@

$(TARGET).elf: $(C_OBJS)
	@echo "Linking ELF: $(notdir $@)"
	@for f in $(C_OBJS); do cygpath -m "$$f" 2>/dev/null || echo "$$f"; done > $(BUILD)/objects.rsp
	@$(LD) $(LDFLAGS) @$(BUILD)/objects.rsp $(LIBS) -o $@
	@python3 $(ROOT)/tools/gen_state_vars.py $@ --nm $(NM) --objcopy $(OBJCOPY) --readelf $(READELF)
	@$(NM) -CSn $@ > $(BUILD)/mzm-3ds.lst
	@echo "Build successful: $(TARGET).elf"

# rcheevos is third-party C89 and compiles clean, so it is built without the
# decomp's warning suppressions -- those exist for the decomp's code and
# should not hide anything here -- and without the decomp's defines, which
# mean nothing to it. -D_3DS is what its rc_compat.h looks for to pick the
# libctru mutex implementation.
CFLAGS_RC := -O2 -fomit-frame-pointer -Wall -Wno-format -mword-relocations \
             -ffunction-sections -fdata-sections -D__3DS__ -D_3DS $(ARCH) $(EXTRA_CFLAGS)
INCLUDES_RC := -I$(RCHEEVOS)/include -I$(RCHEEVOS)/src -I$(RCHEEVOS)/src/rhash \
               -I$(CTRULIB)/include

$(BUILD)/rcheevos/%.o: $(RCHEEVOS)/%.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) -MMD -MP -MF $(@:.o=.d) $(CFLAGS_RC) $(INCLUDES_RC) -c $< -o $@

CFLAGS_GEN := $(patsubst -O2,-O1,$(CFLAGS_DECOMP))

# Compilación de archivos autogenerados (ROM shims) con -O1 para velocidad
$(BUILD)/root/port/generated/%.o: $(ROOT)/port/generated/%.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) -MMD -MP -MF $(@:.o=.d) $(CFLAGS_GEN) $(INCLUDES) -c $< -o $@

# Compilación de archivos C bajo ROOT (decomp + shims): CFLAGS_DECOMP, no CFLAGS.
$(BUILD)/root/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) -MMD -MP -MF $(@:.o=.d) $(CFLAGS_DECOMP) $(INCLUDES) -c $< -o $@

# Compilación de archivos ensamblador bajo asm/
$(BUILD)/root/asm/%.o: $(ROOT)/asm/%.s
	@mkdir -p $(dir $@)
	@echo "AS $<"
	@$(CC) -x assembler-with-cpp -Wa,-I$(ROOT) -march=armv6k -mfloat-abi=hard -mtp=soft -I$(ROOT) -c $< -o $@

# Compilación de archivos C bajo platform/3ds
$(BUILD)/3ds/%.o: $(CURDIR)/%.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) -MMD -MP -MF $(@:.o=.d) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	@echo "Cleaning..."
	@rm -rf $(BUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf

-include $(DEPS)
