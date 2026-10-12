#include "sprite.h"
#include "sprite_debris.h"
#include "syscalls.h"
#include "gba.h"
#include "macros.h"

#ifdef __3DS__
#include "port_sprite_depth_oam.h" /* PORT_SPRITE_DEPTH_* codes */
#endif
#include "fixed_point.h"
#include "sprites_ai/sprites.h"

#include "data/generic_data.h"
#include "data/sprite_data.h"
#include "data/sprites/unused_sprites.h"
#include "data/sprites/charge_beam.h"
#include "data/sprites/deorem.h"
#include "data/sprites/dragon.h"
#include "data/sprites/elevator_pad.h"
#include "data/sprites/enemy_drop.h"
#include "data/sprites/geruta.h"
#include "data/sprites/hive.h"
#include "data/sprites/imago_cocoon.h"
#include "data/sprites/map_station.h"
#include "data/sprites/metroid.h"
#include "data/sprites/message_banner.h"
#include "data/sprites/zoomer.h"
#include "data/sprites/zeela.h"

#include "data/sprites/ripper.h"
#include "data/sprites/zeb.h"
#include "data/sprites/skree.h"
#include "data/sprites/morph_ball.h"
#include "data/sprites/chozo_statue.h"
#include "data/sprites/sova.h"
#include "data/sprites/multiviola.h"
#include "data/sprites/squeept.h"
#include "data/sprites/reo.h"
#include "data/sprites/gunship.h"
#include "data/sprites/skultera.h"
#include "data/sprites/dessgeega.h"
#include "data/sprites/waver.h"
#include "data/sprites/power_grip.h"
#include "data/sprites/imago_larva.h"
#include "data/sprites/morph_ball_launcher.h"
#include "data/sprites/space_pirate.h"
#include "data/sprites/gamet.h"
#include "data/sprites/zebbo.h"
#include "data/sprites/worker_robot.h"
#include "data/sprites/parasite.h"
#include "data/sprites/piston.h"
#include "data/sprites/ridley.h"
#include "data/sprites/security_gate.h"
#include "data/sprites/zipline_generator.h"
#include "data/sprites/polyp.h"
#include "data/sprites/rinka.h"
#include "data/sprites/viola.h"
#include "data/sprites/geron_norfair.h"
#include "data/sprites/holtz.h"
#include "data/sprites/gekitai_machine.h"
#include "data/sprites/ruins_test.h"
#include "data/sprites/save_platform.h"
#include "data/sprites/kraid.h"
#include "data/sprites/ripper2.h"
#include "data/sprites/mella.h"
#include "data/sprites/atomic.h"
#include "data/sprites/area_banner.h"
#include "data/sprites/mother_brain.h"
#include "data/sprites/acid_worm.h"
#include "data/sprites/escape_ship.h"
#include "data/sprites/sidehopper.h"
#include "data/sprites/geega.h"
#include "data/sprites/zebetite_and_cannon.h"
#include "data/sprites/zipline.h"
#include "data/sprites/imago_larva_right_side.h"
#include "data/sprites/tangle_vine.h"
#include "data/sprites/imago.h"
#include "data/sprites/crocomire.h"
#include "data/sprites/geron.h"
#include "data/sprites/glass_tube.h"
#include "data/sprites/save_platform_chozodia.h"
#include "data/sprites/baristute.h"
#include "data/sprites/elevator_statue.h"
#include "data/sprites/rising_chozo_pillar.h"
#include "data/sprites/security_laser.h"
#include "data/sprites/boss_statues.h"
#include "data/sprites/searchlight_eye.h"
#include "data/sprites/steam.h"
#include "data/sprites/unknown_item_block.h"
#include "data/sprites/unknown_item_chozo_statue.h"
#include "data/sprites/gadora.h"
#include "data/sprites/searchlight.h"
#include "data/sprites/space_pirate_carrying_power_bomb.h"
#include "data/sprites/falling_chozo_pillar.h"
#include "data/sprites/mecha_ridley.h"
#include "data/sprites/escape_gate.h"
#include "data/spriteset.h"

#include "constants/game_state.h"
#include "constants/connection.h"
#include "constants/sprite.h"
#include "constants/particle.h"

#include "structs/bg_clip.h"
#include "structs/game_state.h"
#include "structs/room.h"
#include "structs/samus.h"

#define MAX_AMOUNT_OF_SPRITESET 114

static Func_T sPrimarySpritesAIPointers[PSPRITE_COUNT] = {
    [PSPRITE_UNUSED0] = UnusedSprites,
    [PSPRITE_UNUSED1] = UnusedSprites,
    [PSPRITE_UNUSED2] = UnusedSprites,
    [PSPRITE_UNUSED3] = UnusedSprites,
    [PSPRITE_UNUSED4] = UnusedSprites,
    [PSPRITE_UNUSED5] = UnusedSprites,
    [PSPRITE_UNUSED6] = UnusedSprites,
    [PSPRITE_UNUSED7] = UnusedSprites,
    [PSPRITE_UNUSED8] = UnusedSprites,
    [PSPRITE_UNUSED9] = UnusedSprites,
    [PSPRITE_UNUSED10] = UnusedSprites,
    [PSPRITE_UNUSED11] = UnusedSprites,
    [PSPRITE_UNUSED12] = UnusedSprites,
    [PSPRITE_UNUSED13] = UnusedSprites,
    [PSPRITE_UNUSED14] = UnusedSprites,
    [PSPRITE_UNUSED15] = UnusedSprites,
    [PSPRITE_UNUSED16] = UnusedSprites,
    [PSPRITE_MESSAGE_BANNER] = MessageBanner,
    [PSPRITE_ZOOMER_YELLOW] = Zoomer,
    [PSPRITE_ZOOMER_RED] = Zoomer,
    [PSPRITE_ZEELA] = Zeela,
    [PSPRITE_ZEELA_RED] = Zeela,
    [PSPRITE_RIPPER_BROWN] = Ripper,
    [PSPRITE_RIPPER_PURPLE] = Ripper,
    [PSPRITE_ZEB] = Zeb,
    [PSPRITE_ZEB_BLUE] = Zeb,
    [PSPRITE_LARGE_ENERGY_DROP] = EnemyDrop,
    [PSPRITE_SMALL_ENERGY_DROP] = EnemyDrop,
    [PSPRITE_MISSILE_DROP] = EnemyDrop,
    [PSPRITE_SUPER_MISSILE_DROP] = EnemyDrop,
    [PSPRITE_POWER_BOMB_DROP] = EnemyDrop,
    [PSPRITE_SKREE_GREEN] = Skree,
    [PSPRITE_SKREE_BLUE] = Skree,
    [PSPRITE_MORPH_BALL] = MorphBall,
    [PSPRITE_CHOZO_STATUE_LONG_HINT] = ChozoStatue,
    [PSPRITE_CHOZO_STATUE_LONG] = ChozoStatue,
    [PSPRITE_CHOZO_STATUE_ICE_HINT] = ChozoStatue,
    [PSPRITE_CHOZO_STATUE_ICE] = ChozoStatue,
    [PSPRITE_CHOZO_STATUE_WAVE_HINT] = ChozoStatue,
    [PSPRITE_CHOZO_STATUE_WAVE] = ChozoStatue,
    [PSPRITE_CHOZO_STATUE_BOMB_HINT] = ChozoStatue,
    [PSPRITE_CHOZO_STATUE_BOMB] = ChozoStatue,
    [PSPRITE_CHOZO_STATUE_SPEEDBOOSTER_HINT] = ChozoStatue,
    [PSPRITE_CHOZO_STATUE_SPEEDBOOSTER] = ChozoStatue,
    [PSPRITE_CHOZO_STATUE_HIGH_JUMP_HINT] = ChozoStatue,
    [PSPRITE_CHOZO_STATUE_HIGH_JUMP] = ChozoStatue,
    [PSPRITE_CHOZO_STATUE_SCREW_HINT] = ChozoStatue,
    [PSPRITE_CHOZO_STATUE_SCREW] = ChozoStatue,
    [PSPRITE_CHOZO_STATUE_VARIA_HINT] = ChozoStatue,
    [PSPRITE_CHOZO_STATUE_VARIA] = ChozoStatue,
    [PSPRITE_SOVA_PURPLE] = Sova,
    [PSPRITE_SOVA_ORANGE] = Sova,
    [PSPRITE_MULTIVIOLA] = Multiviola,
    [PSPRITE_MULTIPLE_LARGE_ENERGY] = EnemyDrop,
    [PSPRITE_GERUTA_RED] = Geruta,
    [PSPRITE_GERUTA_GREEN] = Geruta,
    [PSPRITE_SQUEEPT] = Squeept,
    [PSPRITE_SQUEEPT_UNUSED] = Squeept,
    [PSPRITE_MAP_STATION] = MapStation,
    [PSPRITE_DRAGON] = Dragon,
    [PSPRITE_DRAGON_UNUSED] = Dragon,
    [PSPRITE_ZIPLINE] = Zipline,
    [PSPRITE_ZIPLINE_BUTTON] = ZiplineButton,
    [PSPRITE_REO_GREEN_WINGS] = Reo,
    [PSPRITE_REO_PURPLE_WINGS] = Reo,
    [PSPRITE_GUNSHIP] = Gunship,
    [PSPRITE_DEOREM_FIRST_LOCATION] = Deorem,
    [PSPRITE_DEOREM_SECOND_LOCATION] = Deorem,
    [PSPRITE_CHARGE_BEAM] = ChargeBeam,
    [PSPRITE_SKULTERA] = Skultera,
    [PSPRITE_DESSGEEGA] = Dessgeega,
    [PSPRITE_DESSGEEGA_AFTER_LONG_BEAM] = Dessgeega,
    [PSPRITE_WAVER] = Waver,
    [PSPRITE_WAVER_UNUSED] = Waver,
    [PSPRITE_MELLOW] = Mellow,
    [PSPRITE_HIVE] = Hive,
    [PSPRITE_POWER_GRIP] = PowerGrip,
    [PSPRITE_IMAGO_LARVA_RIGHT] = ImagoLarva,
    [PSPRITE_MORPH_BALL_LAUNCHER] = MorphBallLauncher,
    [PSPRITE_IMAGO_COCOON] = ImagoCocoon,
    [PSPRITE_ELEVATOR_PAD] = ElevatorPad,
    [PSPRITE_SPACE_PIRATE] = SpacePirate,
    [PSPRITE_SPACE_PIRATE_WAITING1] = SpacePirate,
    [PSPRITE_SPACE_PIRATE_WAITING2] = SpacePirate,
    [PSPRITE_SPACE_PIRATE_WAITING3] = SpacePirate,
    [PSPRITE_SPACE_PIRATE2] = SpacePirate,
    [PSPRITE_GAMET_BLUE_SINGLE] = Gamet,
    [PSPRITE_GAMET_RED_SINGLE] = Gamet,
    [PSPRITE_CHOZO_STATUE_GRAVITY] = UnknownItemChozoStatue,
    [PSPRITE_CHOZO_STATUE_SPACE_JUMP] = UnknownItemChozoStatue,
    [PSPRITE_SECURITY_GATE_DEFAULT_OPEN] = SecurityGateDefaultOpen,
    [PSPRITE_ZEBBO_GREEN] = Zebbo,
    [PSPRITE_ZEBBO_YELLOW] = Zebbo,
    [PSPRITE_WORKER_ROBOT] = WorkerRobot,
    [PSPRITE_PARASITE_MULTIPLE] = ParasiteMultiple,
    [PSPRITE_PARASITE] = Parasite,
    [PSPRITE_PISTON] = Piston,
    [PSPRITE_RIDLEY] = Ridley,
    [PSPRITE_SECURITY_GATE_DEFAULT_CLOSED] = SecurityGateDefaultClosed,
    [PSPRITE_ZIPLINE_GENERATOR] = ZiplineGenerator,
    [PSPRITE_METROID] = Metroid,
    [PSPRITE_FROZEN_METROID] = Metroid,
    [PSPRITE_RINKA_ORANGE] = Rinka,
    [PSPRITE_POLYP] = Polyp,
    [PSPRITE_VIOLA_BLUE] = Viola,
    [PSPRITE_VIOLA_ORANGE] = Viola,
    [PSPRITE_GERON_NORFAIR] = GeronNorfair,
    [PSPRITE_HOLTZ] = Holtz,
    [PSPRITE_GEKITAI_MACHINE] = GekitaiMachine,
    [PSPRITE_RUINS_TEST] = RuinsTest,
    [PSPRITE_SAVE_PLATFORM] = SavePlatform,
    [PSPRITE_KRAID] = Kraid,
    [PSPRITE_IMAGO_COCOON_AFTER_FIGHT] = ImagoCocoonAfterFight,
    [PSPRITE_RIPPERII] = Ripper2,
    [PSPRITE_MELLA] = Mella,
    [PSPRITE_ATOMIC] = Atomic,
    [PSPRITE_AREA_BANNER] = AreaBanner,
    [PSPRITE_MOTHER_BRAIN] = MotherBrain,
    [PSPRITE_FAKE_POWER_BOMB_EVENT_TRIGGER] = FakePowerBombEventTrigger,
    [PSPRITE_ACID_WORM] = AcidWorm,
    [PSPRITE_ESCAPE_SHIP] = EscapeShip,
    [PSPRITE_SIDEHOPPER] = Sidehopper,
    [PSPRITE_GEEGA] = Geega,
    [PSPRITE_GEEGA_WHITE] = Geega,
    [PSPRITE_RINKA_MOTHER_BRAIN] = RinkaMotherBrain,
    [PSPRITE_ZEBETITE_ONE_AND_THREE] = Zebetite,
    [PSPRITE_CANNON] = Cannon,
    [PSPRITE_IMAGO_LARVA_RIGHT_SIDE] = ImagoLarvaRightSide,
    [PSPRITE_TANGLE_VINE_TALL] = TangleVineTall,
    [PSPRITE_TANGLE_VINE_MEDIUM] = TangleVineMedium,
    [PSPRITE_TANGLE_VINE_CURVED] = TangleVineCurved,
    [PSPRITE_TANGLE_VINE_SHORT] = TangleVineShort,
    [PSPRITE_MELLOW_SWARM] = MellowSwarm,
    [PSPRITE_MELLOW_SWARM_HEALTH_BASED] = MellowSwarm,
    [PSPRITE_IMAGO] = Imago,
    [PSPRITE_ZEBETITE_TWO_AND_FOUR] = Zebetite,
    [PSPRITE_CANNON2] = Cannon,
    [PSPRITE_CANNON3] = Cannon,
    [PSPRITE_CROCOMIRE] = Crocomire,
    [PSPRITE_IMAGO_LARVA_LEFT] = ImagoLarva,
    [PSPRITE_GERON_BRINSTAR_ROOM_15] = Geron,
    [PSPRITE_GERON_BRINSTAR_ROOM_1C] = Geron,
    [PSPRITE_GERON_VARIA1] = Geron,
    [PSPRITE_GERON_VARIA2] = Geron,
    [PSPRITE_GERON_VARIA3] = Geron,
    [PSPRITE_GLASS_TUBE] = GlassTube,
    [PSPRITE_SAVE_PLATFORM_CHOZODIA] = SavePlatformChozodia,
    [PSPRITE_BARISTUTE] = Baristute,
    [PSPRITE_CHOZO_STATUE_PLASMA_BEAM] = UnknownItemChozoStatue,
    [PSPRITE_KRAID_ELEVATOR_STATUE] = KraidElevatorStatue,
    [PSPRITE_RIDLEY_ELEVATOR_STATUE] = RidleyElevatorStatue,
    [PSPRITE_RISING_CHOZO_PILLAR] = RisingChozoPillar,
    [PSPRITE_SECURITY_LASER_VERTICAL] = SecurityLaser,
    [PSPRITE_SECURITY_LASER_HORIZONTAL] = SecurityLaser,
    [PSPRITE_SECURITY_LASER_VERTICAL2] = SecurityLaser,
    [PSPRITE_SECURITY_LASER_HORIZONTAL2] = SecurityLaser,
    [PSPRITE_LOCK_UNLOCK_METROID_DOORS_UNUSED] = MetroidDoorLock,
    [PSPRITE_GAMET_BLUE_LEADER] = Gamet,
    [PSPRITE_GAMET_BLUE_FOLLOWER] = Gamet,
    [PSPRITE_GEEGA_LEADER] = Geega,
    [PSPRITE_GEEGA_FOLLOWER] = Geega,
    [PSPRITE_ZEBBO_GREEN_LEADER] = Zebbo,
    [PSPRITE_ZEBBO_GREEN_FOLLOWER] = Zebbo,
    [PSPRITE_KRAID_STATUE] = KraidStatue,
    [PSPRITE_RIDLEY_STATUE] = RidleyStatue,
    [PSPRITE_RINKA_GREEN] = Rinka,
    [PSPRITE_SEARCHLIGHT_EYE] = SearchlightEye,
    [PSPRITE_SEARCHLIGHT_EYE2] = SearchlightEye,
    [PSPRITE_STEAM_LARGE] = Steam,
    [PSPRITE_STEAM_SMALL] = Steam,
    [PSPRITE_PLASMA_BEAM_BLOCK]  = UnknownItemBlock,
    [PSPRITE_GRAVITY_SUIT_BLOCK] = UnknownItemBlock,
    [PSPRITE_SPACE_JUMP_BLOCK] = UnknownItemBlock,
    [PSPRITE_GADORA_KRAID] = Gadora,
    [PSPRITE_GADORA_RIDLEY] = Gadora,
    [PSPRITE_SEARCHLIGHT] = Searchlight,
    [PSPRITE_SEARCHLIGHT2] = Searchlight,
    [PSPRITE_SEARCHLIGHT3] = Searchlight,
    [PSPRITE_SEARCHLIGHT4] = Searchlight,
    [PSPRITE_MAYBE_SEARCHLIGHT_TRIGGER] = PrimarySpriteB3,
    [PSPRITE_DISCOVERED_IMAGO_PASSAGE_EVENT_TRIGGER] = EventTriggerDiscoveredImagoPassage,
    [PSPRITE_FAKE_POWER_BOMB] = FakePowerBomb,
    [PSPRITE_SPACE_PIRATE_CARRYING_POWER_BOMB] = SpacePirateCarryingPowerBomb,
    [PSPRITE_TANGLE_VINE_RED_GARUTA] = TangleVineRedGeruta,
    [PSPRITE_TANGLE_VINE_GERUTA] = TangleVineGeruta,
    [PSPRITE_TANGLE_VINE_LARVA_RIGHT] = TangleVineLarvaRight,
    [PSPRITE_TANGLE_VINE_LARVA_LEFT] = TangleVineLarvaLeft,
    [PSPRITE_WATER_DROP] = WaterDrop,
    [PSPRITE_FALLING_CHOZO_PILLAR] = FallingChozoPillar,
    [PSPRITE_MECHA_RIDLEY] = MechaRidley,
    [PSPRITE_EXPLOSION_ZEBES_ESCAPE] = ExplosionZebesEscape,
    [PSPRITE_STEAM_LARGE_DIAGONAL_UP] = SteamDiagonal,
    [PSPRITE_STEAM_SMALL_DIAGONAL_UP] = SteamDiagonal,
    [PSPRITE_STEAM_LARGE_DIAGONAL_DOWN] = SteamDiagonal,
    [PSPRITE_STEAM_SMALL_DIAGONAL_DOWN] = SteamDiagonal,
    [PSPRITE_BARISTUTE_KRAID_UPPER] = Baristute,
    [PSPRITE_ESCAPE_GATE1] = EscapeGate,
    [PSPRITE_ESCAPE_GATE2] = EscapeGate,
    [PSPRITE_BLACK_SPACE_PIRATE] = BlackSpacePirate,
    [PSPRITE_ESCAPE_SHIP_SPACE_PIRATE] = EscapeShipSpacePirate,
    [PSPRITE_BARISTUTE_KRAID_LOWER] = Baristute,
    [PSPRITE_RINKA_MOTHER_BRAIN2] = RinkaMotherBrain,
    [PSPRITE_RINKA_MOTHER_BRAIN3] = RinkaMotherBrain,
    [PSPRITE_RINKA_MOTHER_BRAIN4] = RinkaMotherBrain,
    [PSPRITE_RINKA_MOTHER_BRAIN5] = RinkaMotherBrain,
    [PSPRITE_RINKA_MOTHER_BRAIN6] = RinkaMotherBrain 
};

#ifdef MZM_3DS
static const u32* sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_COUNT)];
static const u16* sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_COUNT)];

void Init_sSpritesGfxPalPointers(void) {
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_UNUSED16)] = sUnusedSpritesGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MESSAGE_BANNER)] = sMessageBannerGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZOOMER_YELLOW)] = sZoomerGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZOOMER_RED)] = sZoomerGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEELA)] = sZeelaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEELA_RED)] = sZeelaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIPPER_BROWN)] = sRipperBrownGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIPPER_PURPLE)] = sRipperPurpleGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEB)] = sZebPinkGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEB_BLUE)] = sZebBlueGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_LARGE_ENERGY_DROP)] = sZeelaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SMALL_ENERGY_DROP)] = sZeelaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MISSILE_DROP)] = sZeelaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SUPER_MISSILE_DROP)] = sZeelaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_POWER_BOMB_DROP)] = sZeelaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SKREE_GREEN)] = sSkreeGreenGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SKREE_BLUE)] = sSkreeBlueGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MORPH_BALL)] = sMorphBallGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_LONG_HINT)] = sChozoStatueLongBeamGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_LONG)] = sChozoStatueLongBeamGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_ICE_HINT)] = sChozoStatueIceBeamGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_ICE)] = sChozoStatueIceBeamGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_WAVE_HINT)] = sChozoStatueWaveBeamGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_WAVE)] = sChozoStatueWaveBeamGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_BOMB_HINT)] = sChozoStatueBombsGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_BOMB)] = sChozoStatueBombsGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_SPEEDBOOSTER_HINT)] = sChozoStatueSpeedboosterGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_SPEEDBOOSTER)] = sChozoStatueSpeedboosterGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_HIGH_JUMP_HINT)] = sChozoStatueHighJumpGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_HIGH_JUMP)] = sChozoStatueHighJumpGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_SCREW_HINT)] = sChozoStatueScrewAttackGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_SCREW)] = sChozoStatueScrewAttackGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_VARIA_HINT)] = sChozoStatueVariaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_VARIA)] = sChozoStatueVariaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SOVA_PURPLE)] = sSovaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SOVA_ORANGE)] = sSovaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MULTIVIOLA)] = sMultiviolaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MULTIPLE_LARGE_ENERGY)] = sZeelaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERUTA_RED)] = sGerutaRedGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERUTA_GREEN)] = sGerutaGreenGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SQUEEPT)] = sSqueeptGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SQUEEPT_UNUSED)] = sSqueeptGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MAP_STATION)] = sMapStationGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DRAGON)] = sDragonGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DRAGON_UNUSED)] = sDragonGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZIPLINE)] = sZiplineGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZIPLINE_BUTTON)] = sZiplineGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_REO_GREEN_WINGS)] = sReoGreenWingsGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_REO_PURPLE_WINGS)] = sReoPurpleWingsGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GUNSHIP)] = sGunshipGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DEOREM_FIRST_LOCATION)] = sDeoremGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DEOREM_SECOND_LOCATION)] = sDeoremGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHARGE_BEAM)] = sChargeBeamGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SKULTERA)] = sSkulteraGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DESSGEEGA)] = sDessgeegaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DESSGEEGA_AFTER_LONG_BEAM)] = sDessgeegaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_WAVER)] = sWaverGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_WAVER_UNUSED)] = sWaverGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MELLOW)] = sHiveGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_HIVE)] = sHiveGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_POWER_GRIP)] = sPowerGripGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO_LARVA_RIGHT)] = sImagoLarvaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MORPH_BALL_LAUNCHER)] = sMorphBallLauncherGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO_COCOON)] = sImagoCocoonGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ELEVATOR_PAD)] = sElevatorPadGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE)] = sSpacePirateGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE_WAITING1)] = sSpacePirateGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE_WAITING2)] = sSpacePirateGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE_WAITING3)] = sSpacePirateGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE2)] = sSpacePirateGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GAMET_BLUE_SINGLE)] = sGametBlueGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GAMET_RED_SINGLE)] = sGametRedGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_GRAVITY)] = sChozoStatueGravitySuitGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_SPACE_JUMP)] = sChozoStatueSpaceJumpGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_GATE_DEFAULT_OPEN)] = sSecurityGateGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBBO_GREEN)] = sZebboGreenGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBBO_YELLOW)] = sZebboYellowGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_WORKER_ROBOT)] = sWorkerRobotGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_PARASITE_MULTIPLE)] = sParasiteGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_PARASITE)] = sParasiteGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_PISTON)] = sPistonGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIDLEY)] = sRidleyGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_GATE_DEFAULT_CLOSED)] = sSecurityGateGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZIPLINE_GENERATOR)] = sZiplineGeneratorGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_METROID)] = sMetroidGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_FROZEN_METROID)] = sMetroidGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_ORANGE)] = sRinkaOrangeGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_POLYP)] = sPolypGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_VIOLA_BLUE)] = sViolaBlueGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_VIOLA_ORANGE)] = sViolaOrangeGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_NORFAIR)] = sGeronNorfairGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_HOLTZ)] = sHoltzGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GEKITAI_MACHINE)] = sGekitaiMachineGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RUINS_TEST)] = sRuinsTestGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SAVE_PLATFORM)] = sSavePlatformGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_KRAID)] = sKraidGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO_COCOON_AFTER_FIGHT)] = sImagoCocoonGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIPPERII)] = sRipper2Gfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MELLA)] = sMellaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ATOMIC)] = sAtomicGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_AREA_BANNER)] = sAreaBannerGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MOTHER_BRAIN)] = sMotherBrainGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_FAKE_POWER_BOMB_EVENT_TRIGGER)] = sZeelaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ACID_WORM)] = sAcidWormGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ESCAPE_SHIP)] = sEscapeShipGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SIDEHOPPER)] = sSidehopperGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GEEGA)] = sGeegaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GEEGA_WHITE)] = sGeegaWhiteGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN)] = sRinkaZebetiteAndCannonGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBETITE_ONE_AND_THREE)] = sRinkaZebetiteAndCannonGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CANNON)] = sRinkaZebetiteAndCannonGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO_LARVA_RIGHT_SIDE)] = sImagoLarvaRightSideGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_TALL)] = sTangleVineGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_MEDIUM)] = sTangleVineGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_CURVED)] = sTangleVineGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_SHORT)] = sTangleVineGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MELLOW_SWARM)] = sHiveGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MELLOW_SWARM_HEALTH_BASED)] = sHiveGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO)] = sImagoGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBETITE_TWO_AND_FOUR)] = sRinkaZebetiteAndCannonGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CANNON2)] = sRinkaZebetiteAndCannonGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CANNON3)] = sRinkaZebetiteAndCannonGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CROCOMIRE)] = sCrocomireGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO_LARVA_LEFT)] = sImagoLarvaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_BRINSTAR_ROOM_15)] = sGeronGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_BRINSTAR_ROOM_1C)] = sGeronGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_VARIA1)] = sGeronGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_VARIA2)] = sGeronGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_VARIA3)] = sGeronGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GLASS_TUBE)] = sGlassTubeGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SAVE_PLATFORM_CHOZODIA)] = sSavePlatformChozodiaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_BARISTUTE)] = sBaristuteGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_PLASMA_BEAM)] = sChozoStatuePlasmaBeamGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_KRAID_ELEVATOR_STATUE)] = sElevatorStatuesGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIDLEY_ELEVATOR_STATUE)] = sElevatorStatuesGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RISING_CHOZO_PILLAR)] = sRisingChozoPillarGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_LASER_VERTICAL)] = sSecurityLaserGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_LASER_HORIZONTAL)] = sSecurityLaserGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_LASER_VERTICAL2)] = sSecurityLaserGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_LASER_HORIZONTAL2)] = sSecurityLaserGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_LOCK_UNLOCK_METROID_DOORS_UNUSED)] = sZeelaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GAMET_BLUE_LEADER)] = sGametBlueGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GAMET_BLUE_FOLLOWER)] = sGametBlueGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GEEGA_LEADER)] = sGeegaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GEEGA_FOLLOWER)] = sGeegaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBBO_GREEN_LEADER)] = sZebboGreenGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBBO_GREEN_FOLLOWER)] = sZebboGreenGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_KRAID_STATUE)] = sBossStatuesGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIDLEY_STATUE)] = sBossStatuesGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_GREEN)] = sRinkaGreenGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT_EYE)] = sSearchlightEyeGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT_EYE2)] = sSearchlightEyeGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_LARGE)] = sSteamGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_SMALL)] = sSteamGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_PLASMA_BEAM_BLOCK)] = sPlasmaBeamBlockGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GRAVITY_SUIT_BLOCK)] = sGravityBlockGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_JUMP_BLOCK)] = sSpaceJumpBlockGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GADORA_KRAID)] = sGadoraGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GADORA_RIDLEY)] = sGadoraGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT)] = sSearchlightGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT2)] = sSearchlightGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT3)] = sSearchlightGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT4)] = sSearchlightGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MAYBE_SEARCHLIGHT_TRIGGER)] = sZeelaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DISCOVERED_IMAGO_PASSAGE_EVENT_TRIGGER)] = sZeelaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_FAKE_POWER_BOMB)] = sFakePowerBombGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE_CARRYING_POWER_BOMB)] = sSpacePirateGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_RED_GARUTA)] = sTangleVineGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_GERUTA)] = sTangleVineGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_LARVA_RIGHT)] = sTangleVineGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_LARVA_LEFT)] = sTangleVineGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_WATER_DROP)] = sZeelaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_FALLING_CHOZO_PILLAR)] = sFallingChozoPillarGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MECHA_RIDLEY)] = sMechaRidleyGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_EXPLOSION_ZEBES_ESCAPE)] = sZeelaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_LARGE_DIAGONAL_UP)] = sSteamGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_SMALL_DIAGONAL_UP)] = sSteamGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_LARGE_DIAGONAL_DOWN)] = sSteamGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_SMALL_DIAGONAL_DOWN)] = sSteamGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_BARISTUTE_KRAID_UPPER)] = sBaristuteGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ESCAPE_GATE1)] = sZeelaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ESCAPE_GATE2)] = sZeelaGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_BLACK_SPACE_PIRATE)] = sSpacePirateGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ESCAPE_SHIP_SPACE_PIRATE)] = sSpacePirateGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_BARISTUTE_KRAID_LOWER)] = sBaristuteGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN2)] = sRinkaZebetiteAndCannonGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN3)] = sRinkaZebetiteAndCannonGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN4)] = sRinkaZebetiteAndCannonGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN5)] = sRinkaZebetiteAndCannonGfx;
    sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN6)] = sRinkaZebetiteAndCannonGfx;

    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_UNUSED16)] = sUnusedSpritesPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MESSAGE_BANNER)] = sMessageBannerPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZOOMER_YELLOW)] = sZoomerPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZOOMER_RED)] = sZoomerPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEELA)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEELA_RED)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIPPER_BROWN)] = sRipperBrownPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIPPER_PURPLE)] = sRipperPurplePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEB)] = sZebPinkPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEB_BLUE)] = sZebBluePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_LARGE_ENERGY_DROP)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SMALL_ENERGY_DROP)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MISSILE_DROP)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SUPER_MISSILE_DROP)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_POWER_BOMB_DROP)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SKREE_GREEN)] = sSkreeGreenPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SKREE_BLUE)] = sSkreeBluePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MORPH_BALL)] = sMorphBallPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_LONG_HINT)] = sChozoStatueLongBeamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_LONG)] = sChozoStatueLongBeamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_ICE_HINT)] = sChozoStatueIceBeamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_ICE)] = sChozoStatueIceBeamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_WAVE_HINT)] = sChozoStatueWaveBeamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_WAVE)] = sChozoStatueWaveBeamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_BOMB_HINT)] = sChozoStatueBombsPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_BOMB)] = sChozoStatueBombsPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_SPEEDBOOSTER_HINT)] = sChozoStatueSpeedboosterPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_SPEEDBOOSTER)] = sChozoStatueSpeedboosterPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_HIGH_JUMP_HINT)] = sChozoStatueHighJumpPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_HIGH_JUMP)] = sChozoStatueHighJumpPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_SCREW_HINT)] = sChozoStatueScrewAttackPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_SCREW)] = sChozoStatueScrewAttackPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_VARIA_HINT)] = sChozoStatueVariaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_VARIA)] = sChozoStatueVariaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SOVA_PURPLE)] = sSovaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SOVA_ORANGE)] = sSovaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MULTIVIOLA)] = sMultiviolaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MULTIPLE_LARGE_ENERGY)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERUTA_RED)] = sGerutaRedPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERUTA_GREEN)] = sGerutaGreenPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SQUEEPT)] = sSqueeptPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SQUEEPT_UNUSED)] = sSqueeptPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MAP_STATION)] = sMapStationPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DRAGON)] = sDragonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DRAGON_UNUSED)] = sDragonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZIPLINE)] = sZiplinePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZIPLINE_BUTTON)] = sZiplinePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_REO_GREEN_WINGS)] = sReoGreenWingsPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_REO_PURPLE_WINGS)] = sReoPurpleWingsPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GUNSHIP)] = sGunshipPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DEOREM_FIRST_LOCATION)] = sDeoremPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DEOREM_SECOND_LOCATION)] = sDeoremPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHARGE_BEAM)] = sChargeBeamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SKULTERA)] = sSkulteraPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DESSGEEGA)] = sDessgeegaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DESSGEEGA_AFTER_LONG_BEAM)] = sDessgeegaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_WAVER)] = sWaverPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_WAVER_UNUSED)] = sWaverPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MELLOW)] = sHivePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_HIVE)] = sHivePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_POWER_GRIP)] = sPowerGripPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO_LARVA_RIGHT)] = sImagoLarvaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MORPH_BALL_LAUNCHER)] = sMorphBallLauncherPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO_COCOON)] = sImagoCocoonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ELEVATOR_PAD)] = sElevatorPadPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE)] = sSpacePiratePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE_WAITING1)] = sSpacePiratePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE_WAITING2)] = sSpacePiratePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE_WAITING3)] = sSpacePiratePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE2)] = sSpacePiratePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GAMET_BLUE_SINGLE)] = sGametBluePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GAMET_RED_SINGLE)] = sGametRedPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_GRAVITY)] = sChozoStatueGravitySuitPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_SPACE_JUMP)] = sChozoStatueSpaceJumpPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_GATE_DEFAULT_OPEN)] = sSecurityGatePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBBO_GREEN)] = sZebboGreenPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBBO_YELLOW)] = sZebboYellowPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_WORKER_ROBOT)] = sWorkerRobotPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_PARASITE_MULTIPLE)] = sParasitePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_PARASITE)] = sParasitePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_PISTON)] = sPistonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIDLEY)] = sRidleyPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_GATE_DEFAULT_CLOSED)] = sSecurityGatePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZIPLINE_GENERATOR)] = sZiplineGeneratorPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_METROID)] = sMetroidPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_FROZEN_METROID)] = sMetroidPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_ORANGE)] = sRinkaOrangePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_POLYP)] = sPolypPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_VIOLA_BLUE)] = sViolaBluePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_VIOLA_ORANGE)] = sViolaOrangePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_NORFAIR)] = sGeronNorfairPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_HOLTZ)] = sHoltzPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GEKITAI_MACHINE)] = sGekitaiMachinePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RUINS_TEST)] = sRuinsTestPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SAVE_PLATFORM)] = sSavePlatformPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_KRAID)] = sKraidPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO_COCOON_AFTER_FIGHT)] = sImagoCocoonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIPPERII)] = sRipper2Pal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MELLA)] = sMellaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ATOMIC)] = sAtomicPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_AREA_BANNER)] = sAreaBannerPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MOTHER_BRAIN)] = sMotherBrainPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_FAKE_POWER_BOMB_EVENT_TRIGGER)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ACID_WORM)] = sAcidWormPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ESCAPE_SHIP)] = sEscapeShipPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SIDEHOPPER)] = sSidehopperPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GEEGA)] = sGeegaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GEEGA_WHITE)] = sGeegaWhitePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN)] = sRinkaZebetiteAndCannonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBETITE_ONE_AND_THREE)] = sRinkaZebetiteAndCannonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CANNON)] = sRinkaZebetiteAndCannonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO_LARVA_RIGHT_SIDE)] = sImagoLarvaRightSidePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_TALL)] = sTangleVinePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_MEDIUM)] = sTangleVinePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_CURVED)] = sTangleVinePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_SHORT)] = sTangleVinePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MELLOW_SWARM)] = sHivePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MELLOW_SWARM_HEALTH_BASED)] = sHivePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO)] = sImagoPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBETITE_TWO_AND_FOUR)] = sRinkaZebetiteAndCannonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CANNON2)] = sRinkaZebetiteAndCannonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CANNON3)] = sRinkaZebetiteAndCannonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CROCOMIRE)] = sCrocomirePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO_LARVA_LEFT)] = sImagoLarvaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_BRINSTAR_ROOM_15)] = sGeronPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_BRINSTAR_ROOM_1C)] = sGeronPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_VARIA1)] = sGeronPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_VARIA2)] = sGeronPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_VARIA3)] = sGeronPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GLASS_TUBE)] = sGlassTubePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SAVE_PLATFORM_CHOZODIA)] = sSavePlatformChozodiaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_BARISTUTE)] = sBaristutePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_PLASMA_BEAM)] = sChozoStatuePlasmaBeamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_KRAID_ELEVATOR_STATUE)] = sElevatorStatuesPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIDLEY_ELEVATOR_STATUE)] = sElevatorStatuesPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RISING_CHOZO_PILLAR)] = sRisingChozoPillarPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_LASER_VERTICAL)] = sSecurityLaserPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_LASER_HORIZONTAL)] = sSecurityLaserPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_LASER_VERTICAL2)] = sSecurityLaserPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_LASER_HORIZONTAL2)] = sSecurityLaserPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_LOCK_UNLOCK_METROID_DOORS_UNUSED)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GAMET_BLUE_LEADER)] = sGametBluePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GAMET_BLUE_FOLLOWER)] = sGametBluePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GEEGA_LEADER)] = sGeegaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GEEGA_FOLLOWER)] = sGeegaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBBO_GREEN_LEADER)] = sZebboGreenPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBBO_GREEN_FOLLOWER)] = sZebboGreenPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_KRAID_STATUE)] = sBossStatuesPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIDLEY_STATUE)] = sBossStatuesPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_GREEN)] = sRinkaGreenPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT_EYE)] = sSearchlightEyePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT_EYE2)] = sSearchlightEyePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_LARGE)] = sSteamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_SMALL)] = sSteamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_PLASMA_BEAM_BLOCK)] = sPlasmaBeamBlockPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GRAVITY_SUIT_BLOCK)] = sGravityBlockPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_JUMP_BLOCK)] = sSpaceJumpBlockPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GADORA_KRAID)] = sGadoraPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GADORA_RIDLEY)] = sGadoraPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT)] = sSearchlightPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT2)] = sSearchlightPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT3)] = sSearchlightPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT4)] = sSearchlightPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MAYBE_SEARCHLIGHT_TRIGGER)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DISCOVERED_IMAGO_PASSAGE_EVENT_TRIGGER)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_FAKE_POWER_BOMB)] = sFakePowerBombPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE_CARRYING_POWER_BOMB)] = sSpacePiratePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_RED_GARUTA)] = sTangleVinePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_GERUTA)] = sTangleVinePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_LARVA_RIGHT)] = sTangleVinePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_LARVA_LEFT)] = sTangleVinePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_WATER_DROP)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_FALLING_CHOZO_PILLAR)] = sFallingChozoPillarPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MECHA_RIDLEY)] = sMechaRidleyPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_EXPLOSION_ZEBES_ESCAPE)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_LARGE_DIAGONAL_UP)] = sSteamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_SMALL_DIAGONAL_UP)] = sSteamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_LARGE_DIAGONAL_DOWN)] = sSteamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_SMALL_DIAGONAL_DOWN)] = sSteamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_BARISTUTE_KRAID_UPPER)] = sBaristutePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ESCAPE_GATE1)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ESCAPE_GATE2)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_BLACK_SPACE_PIRATE)] = sSpacePiratePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ESCAPE_SHIP_SPACE_PIRATE)] = sSpacePiratePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_BARISTUTE_KRAID_LOWER)] = sBaristutePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN2)] = sRinkaZebetiteAndCannonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN3)] = sRinkaZebetiteAndCannonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN4)] = sRinkaZebetiteAndCannonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN5)] = sRinkaZebetiteAndCannonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN6)] = sRinkaZebetiteAndCannonPal;
}
#else
static const u32* sSpritesGraphicsPointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_COUNT)] = {
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_UNUSED16)] = sUnusedSpritesGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MESSAGE_BANNER)] = sMessageBannerGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZOOMER_YELLOW)] = sZoomerGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZOOMER_RED)] = sZoomerGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEELA)] = sZeelaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEELA_RED)] = sZeelaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIPPER_BROWN)] = sRipperBrownGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIPPER_PURPLE)] = sRipperPurpleGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEB)] = sZebPinkGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEB_BLUE)] = sZebBlueGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_LARGE_ENERGY_DROP)] = sZeelaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SMALL_ENERGY_DROP)] = sZeelaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MISSILE_DROP)] = sZeelaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SUPER_MISSILE_DROP)] = sZeelaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_POWER_BOMB_DROP)] = sZeelaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SKREE_GREEN)] = sSkreeGreenGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SKREE_BLUE)] = sSkreeBlueGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MORPH_BALL)] = sMorphBallGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_LONG_HINT)] = sChozoStatueLongBeamGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_LONG)] = sChozoStatueLongBeamGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_ICE_HINT)] = sChozoStatueIceBeamGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_ICE)] = sChozoStatueIceBeamGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_WAVE_HINT)] = sChozoStatueWaveBeamGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_WAVE)] = sChozoStatueWaveBeamGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_BOMB_HINT)] = sChozoStatueBombsGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_BOMB)] = sChozoStatueBombsGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_SPEEDBOOSTER_HINT)] = sChozoStatueSpeedboosterGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_SPEEDBOOSTER)] = sChozoStatueSpeedboosterGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_HIGH_JUMP_HINT)] = sChozoStatueHighJumpGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_HIGH_JUMP)] = sChozoStatueHighJumpGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_SCREW_HINT)] = sChozoStatueScrewAttackGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_SCREW)] = sChozoStatueScrewAttackGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_VARIA_HINT)] = sChozoStatueVariaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_VARIA)] = sChozoStatueVariaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SOVA_PURPLE)] = sSovaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SOVA_ORANGE)] = sSovaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MULTIVIOLA)] = sMultiviolaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MULTIPLE_LARGE_ENERGY)] = sZeelaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERUTA_RED)] = sGerutaRedGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERUTA_GREEN)] = sGerutaGreenGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SQUEEPT)] = sSqueeptGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SQUEEPT_UNUSED)] = sSqueeptGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MAP_STATION)] = sMapStationGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DRAGON)] = sDragonGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DRAGON_UNUSED)] = sDragonGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZIPLINE)] = sZiplineGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZIPLINE_BUTTON)] = sZiplineGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_REO_GREEN_WINGS)] = sReoGreenWingsGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_REO_PURPLE_WINGS)] = sReoPurpleWingsGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GUNSHIP)] = sGunshipGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DEOREM_FIRST_LOCATION)] = sDeoremGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DEOREM_SECOND_LOCATION)] = sDeoremGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHARGE_BEAM)] = sChargeBeamGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SKULTERA)] = sSkulteraGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DESSGEEGA)] = sDessgeegaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DESSGEEGA_AFTER_LONG_BEAM)] = sDessgeegaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_WAVER)] = sWaverGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_WAVER_UNUSED)] = sWaverGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MELLOW)] = sHiveGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_HIVE)] = sHiveGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_POWER_GRIP)] = sPowerGripGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO_LARVA_RIGHT)] = sImagoLarvaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MORPH_BALL_LAUNCHER)] = sMorphBallLauncherGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO_COCOON)] = sImagoCocoonGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ELEVATOR_PAD)] = sElevatorPadGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE)] = sSpacePirateGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE_WAITING1)] = sSpacePirateGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE_WAITING2)] = sSpacePirateGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE_WAITING3)] = sSpacePirateGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE2)] = sSpacePirateGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GAMET_BLUE_SINGLE)] = sGametBlueGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GAMET_RED_SINGLE)] = sGametRedGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_GRAVITY)] = sChozoStatueGravitySuitGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_SPACE_JUMP)] = sChozoStatueSpaceJumpGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_GATE_DEFAULT_OPEN)] = sSecurityGateGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBBO_GREEN)] = sZebboGreenGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBBO_YELLOW)] = sZebboYellowGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_WORKER_ROBOT)] = sWorkerRobotGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_PARASITE_MULTIPLE)] = sParasiteGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_PARASITE)] = sParasiteGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_PISTON)] = sPistonGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIDLEY)] = sRidleyGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_GATE_DEFAULT_CLOSED)] = sSecurityGateGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZIPLINE_GENERATOR)] = sZiplineGeneratorGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_METROID)] = sMetroidGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_FROZEN_METROID)] = sMetroidGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_ORANGE)] = sRinkaOrangeGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_POLYP)] = sPolypGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_VIOLA_BLUE)] = sViolaBlueGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_VIOLA_ORANGE)] = sViolaOrangeGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_NORFAIR)] = sGeronNorfairGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_HOLTZ)] = sHoltzGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GEKITAI_MACHINE)] = sGekitaiMachineGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RUINS_TEST)] = sRuinsTestGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SAVE_PLATFORM)] = sSavePlatformGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_KRAID)] = sKraidGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO_COCOON_AFTER_FIGHT)] = sImagoCocoonGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIPPERII)] = sRipper2Gfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MELLA)] = sMellaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ATOMIC)] = sAtomicGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_AREA_BANNER)] = sAreaBannerGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MOTHER_BRAIN)] = sMotherBrainGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_FAKE_POWER_BOMB_EVENT_TRIGGER)] = sZeelaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ACID_WORM)] = sAcidWormGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ESCAPE_SHIP)] = sEscapeShipGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SIDEHOPPER)] = sSidehopperGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GEEGA)] = sGeegaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GEEGA_WHITE)] = sGeegaWhiteGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN)] = sRinkaZebetiteAndCannonGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBETITE_ONE_AND_THREE)] = sRinkaZebetiteAndCannonGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CANNON)] = sRinkaZebetiteAndCannonGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO_LARVA_RIGHT_SIDE)] = sImagoLarvaRightSideGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_TALL)] = sTangleVineGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_MEDIUM)] = sTangleVineGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_CURVED)] = sTangleVineGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_SHORT)] = sTangleVineGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MELLOW_SWARM)] = sHiveGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MELLOW_SWARM_HEALTH_BASED)] = sHiveGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO)] = sImagoGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBETITE_TWO_AND_FOUR)] = sRinkaZebetiteAndCannonGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CANNON2)] = sRinkaZebetiteAndCannonGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CANNON3)] = sRinkaZebetiteAndCannonGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CROCOMIRE)] = sCrocomireGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO_LARVA_LEFT)] = sImagoLarvaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_BRINSTAR_ROOM_15)] = sGeronGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_BRINSTAR_ROOM_1C)] = sGeronGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_VARIA1)] = sGeronGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_VARIA2)] = sGeronGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_VARIA3)] = sGeronGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GLASS_TUBE)] = sGlassTubeGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SAVE_PLATFORM_CHOZODIA)] = sSavePlatformChozodiaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_BARISTUTE)] = sBaristuteGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_PLASMA_BEAM)] = sChozoStatuePlasmaBeamGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_KRAID_ELEVATOR_STATUE)] = sElevatorStatuesGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIDLEY_ELEVATOR_STATUE)] = sElevatorStatuesGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RISING_CHOZO_PILLAR)] = sRisingChozoPillarGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_LASER_VERTICAL)] = sSecurityLaserGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_LASER_HORIZONTAL)] = sSecurityLaserGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_LASER_VERTICAL2)] = sSecurityLaserGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_LASER_HORIZONTAL2)] = sSecurityLaserGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_LOCK_UNLOCK_METROID_DOORS_UNUSED)] = sZeelaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GAMET_BLUE_LEADER)] = sGametBlueGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GAMET_BLUE_FOLLOWER)] = sGametBlueGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GEEGA_LEADER)] = sGeegaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GEEGA_FOLLOWER)] = sGeegaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBBO_GREEN_LEADER)] = sZebboGreenGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBBO_GREEN_FOLLOWER)] = sZebboGreenGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_KRAID_STATUE)] = sBossStatuesGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIDLEY_STATUE)] = sBossStatuesGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_GREEN)] = sRinkaGreenGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT_EYE)] = sSearchlightEyeGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT_EYE2)] = sSearchlightEyeGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_LARGE)] = sSteamGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_SMALL)] = sSteamGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_PLASMA_BEAM_BLOCK)] = sPlasmaBeamBlockGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GRAVITY_SUIT_BLOCK)] = sGravityBlockGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_JUMP_BLOCK)] = sSpaceJumpBlockGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GADORA_KRAID)] = sGadoraGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GADORA_RIDLEY)] = sGadoraGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT)] = sSearchlightGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT2)] = sSearchlightGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT3)] = sSearchlightGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT4)] = sSearchlightGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MAYBE_SEARCHLIGHT_TRIGGER)] = sZeelaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DISCOVERED_IMAGO_PASSAGE_EVENT_TRIGGER)] = sZeelaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_FAKE_POWER_BOMB)] = sFakePowerBombGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE_CARRYING_POWER_BOMB)] = sSpacePirateGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_RED_GARUTA)] = sTangleVineGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_GERUTA)] = sTangleVineGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_LARVA_RIGHT)] = sTangleVineGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_LARVA_LEFT)] = sTangleVineGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_WATER_DROP)] = sZeelaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_FALLING_CHOZO_PILLAR)] = sFallingChozoPillarGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MECHA_RIDLEY)] = sMechaRidleyGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_EXPLOSION_ZEBES_ESCAPE)] = sZeelaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_LARGE_DIAGONAL_UP)] = sSteamGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_SMALL_DIAGONAL_UP)] = sSteamGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_LARGE_DIAGONAL_DOWN)] = sSteamGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_SMALL_DIAGONAL_DOWN)] = sSteamGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_BARISTUTE_KRAID_UPPER)] = sBaristuteGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ESCAPE_GATE1)] = sZeelaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ESCAPE_GATE2)] = sZeelaGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_BLACK_SPACE_PIRATE)] = sSpacePirateGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ESCAPE_SHIP_SPACE_PIRATE)] = sSpacePirateGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_BARISTUTE_KRAID_LOWER)] = sBaristuteGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN2)] = sRinkaZebetiteAndCannonGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN3)] = sRinkaZebetiteAndCannonGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN4)] = sRinkaZebetiteAndCannonGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN5)] = sRinkaZebetiteAndCannonGfx,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN6)] = sRinkaZebetiteAndCannonGfx
};

#ifdef MZM_3DS
static const u16* sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_COUNT)];
void Init_sSpritesPalettePointers(void) {
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_UNUSED16)] = sUnusedSpritesPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MESSAGE_BANNER)] = sMessageBannerPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZOOMER_YELLOW)] = sZoomerPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZOOMER_RED)] = sZoomerPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEELA)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEELA_RED)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIPPER_BROWN)] = sRipperBrownPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIPPER_PURPLE)] = sRipperPurplePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEB)] = sZebPinkPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEB_BLUE)] = sZebBluePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_LARGE_ENERGY_DROP)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SMALL_ENERGY_DROP)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MISSILE_DROP)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SUPER_MISSILE_DROP)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_POWER_BOMB_DROP)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SKREE_GREEN)] = sSkreeGreenPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SKREE_BLUE)] = sSkreeBluePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MORPH_BALL)] = sMorphBallPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_LONG_HINT)] = sChozoStatueLongBeamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_LONG)] = sChozoStatueLongBeamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_ICE_HINT)] = sChozoStatueIceBeamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_ICE)] = sChozoStatueIceBeamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_WAVE_HINT)] = sChozoStatueWaveBeamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_WAVE)] = sChozoStatueWaveBeamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_BOMB_HINT)] = sChozoStatueBombsPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_BOMB)] = sChozoStatueBombsPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_SPEEDBOOSTER_HINT)] = sChozoStatueSpeedboosterPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_SPEEDBOOSTER)] = sChozoStatueSpeedboosterPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_HIGH_JUMP_HINT)] = sChozoStatueHighJumpPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_HIGH_JUMP)] = sChozoStatueHighJumpPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_SCREW_HINT)] = sChozoStatueScrewAttackPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_SCREW)] = sChozoStatueScrewAttackPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_VARIA_HINT)] = sChozoStatueVariaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_VARIA)] = sChozoStatueVariaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SOVA_PURPLE)] = sSovaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SOVA_ORANGE)] = sSovaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MULTIVIOLA)] = sMultiviolaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MULTIPLE_LARGE_ENERGY)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERUTA_RED)] = sGerutaRedPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERUTA_GREEN)] = sGerutaGreenPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SQUEEPT)] = sSqueeptPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SQUEEPT_UNUSED)] = sSqueeptPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MAP_STATION)] = sMapStationPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DRAGON)] = sDragonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DRAGON_UNUSED)] = sDragonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZIPLINE)] = sZiplinePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZIPLINE_BUTTON)] = sZiplinePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_REO_GREEN_WINGS)] = sReoGreenWingsPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_REO_PURPLE_WINGS)] = sReoPurpleWingsPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GUNSHIP)] = sGunshipPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DEOREM_FIRST_LOCATION)] = sDeoremPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DEOREM_SECOND_LOCATION)] = sDeoremPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHARGE_BEAM)] = sChargeBeamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SKULTERA)] = sSkulteraPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DESSGEEGA)] = sDessgeegaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DESSGEEGA_AFTER_LONG_BEAM)] = sDessgeegaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_WAVER)] = sWaverPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_WAVER_UNUSED)] = sWaverPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MELLOW)] = sHivePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_HIVE)] = sHivePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_POWER_GRIP)] = sPowerGripPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO_LARVA_RIGHT)] = sImagoLarvaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MORPH_BALL_LAUNCHER)] = sMorphBallLauncherPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO_COCOON)] = sImagoCocoonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ELEVATOR_PAD)] = sElevatorPadPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE)] = sSpacePiratePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE_WAITING1)] = sSpacePiratePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE_WAITING2)] = sSpacePiratePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE_WAITING3)] = sSpacePiratePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE2)] = sSpacePiratePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GAMET_BLUE_SINGLE)] = sGametBluePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GAMET_RED_SINGLE)] = sGametRedPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_GRAVITY)] = sChozoStatueGravitySuitPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_SPACE_JUMP)] = sChozoStatueSpaceJumpPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_GATE_DEFAULT_OPEN)] = sSecurityGatePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBBO_GREEN)] = sZebboGreenPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBBO_YELLOW)] = sZebboYellowPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_WORKER_ROBOT)] = sWorkerRobotPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_PARASITE_MULTIPLE)] = sParasitePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_PARASITE)] = sParasitePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_PISTON)] = sPistonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIDLEY)] = sRidleyPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_GATE_DEFAULT_CLOSED)] = sSecurityGatePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZIPLINE_GENERATOR)] = sZiplineGeneratorPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_METROID)] = sMetroidPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_FROZEN_METROID)] = sMetroidPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_ORANGE)] = sRinkaOrangePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_POLYP)] = sPolypPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_VIOLA_BLUE)] = sViolaBluePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_VIOLA_ORANGE)] = sViolaOrangePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_NORFAIR)] = sGeronNorfairPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_HOLTZ)] = sHoltzPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GEKITAI_MACHINE)] = sGekitaiMachinePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RUINS_TEST)] = sRuinsTestPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SAVE_PLATFORM)] = sSavePlatformPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_KRAID)] = sKraidPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO_COCOON_AFTER_FIGHT)] = sImagoCocoonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIPPERII)] = sRipper2Pal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MELLA)] = sMellaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ATOMIC)] = sAtomicPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_AREA_BANNER)] = sAreaBannerPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MOTHER_BRAIN)] = sMotherBrainPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_FAKE_POWER_BOMB_EVENT_TRIGGER)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ACID_WORM)] = sAcidWormPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ESCAPE_SHIP)] = sEscapeShipPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SIDEHOPPER)] = sSidehopperPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GEEGA)] = sGeegaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GEEGA_WHITE)] = sGeegaWhitePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN)] = sRinkaZebetiteAndCannonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBETITE_ONE_AND_THREE)] = sRinkaZebetiteAndCannonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CANNON)] = sRinkaZebetiteAndCannonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO_LARVA_RIGHT_SIDE)] = sImagoLarvaRightSidePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_TALL)] = sTangleVinePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_MEDIUM)] = sTangleVinePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_CURVED)] = sTangleVinePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_SHORT)] = sTangleVinePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MELLOW_SWARM)] = sHivePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MELLOW_SWARM_HEALTH_BASED)] = sHivePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO)] = sImagoPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBETITE_TWO_AND_FOUR)] = sRinkaZebetiteAndCannonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CANNON2)] = sRinkaZebetiteAndCannonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CANNON3)] = sRinkaZebetiteAndCannonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CROCOMIRE)] = sCrocomirePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO_LARVA_LEFT)] = sImagoLarvaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_BRINSTAR_ROOM_15)] = sGeronPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_BRINSTAR_ROOM_1C)] = sGeronPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_VARIA1)] = sGeronPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_VARIA2)] = sGeronPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_VARIA3)] = sGeronPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GLASS_TUBE)] = sGlassTubePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SAVE_PLATFORM_CHOZODIA)] = sSavePlatformChozodiaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_BARISTUTE)] = sBaristutePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_PLASMA_BEAM)] = sChozoStatuePlasmaBeamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_KRAID_ELEVATOR_STATUE)] = sElevatorStatuesPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIDLEY_ELEVATOR_STATUE)] = sElevatorStatuesPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RISING_CHOZO_PILLAR)] = sRisingChozoPillarPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_LASER_VERTICAL)] = sSecurityLaserPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_LASER_HORIZONTAL)] = sSecurityLaserPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_LASER_VERTICAL2)] = sSecurityLaserPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_LASER_HORIZONTAL2)] = sSecurityLaserPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_LOCK_UNLOCK_METROID_DOORS_UNUSED)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GAMET_BLUE_LEADER)] = sGametBluePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GAMET_BLUE_FOLLOWER)] = sGametBluePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GEEGA_LEADER)] = sGeegaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GEEGA_FOLLOWER)] = sGeegaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBBO_GREEN_LEADER)] = sZebboGreenPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBBO_GREEN_FOLLOWER)] = sZebboGreenPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_KRAID_STATUE)] = sBossStatuesPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIDLEY_STATUE)] = sBossStatuesPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_GREEN)] = sRinkaGreenPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT_EYE)] = sSearchlightEyePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT_EYE2)] = sSearchlightEyePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_LARGE)] = sSteamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_SMALL)] = sSteamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_PLASMA_BEAM_BLOCK)] = sPlasmaBeamBlockPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GRAVITY_SUIT_BLOCK)] = sGravityBlockPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_JUMP_BLOCK)] = sSpaceJumpBlockPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GADORA_KRAID)] = sGadoraPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GADORA_RIDLEY)] = sGadoraPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT)] = sSearchlightPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT2)] = sSearchlightPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT3)] = sSearchlightPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT4)] = sSearchlightPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MAYBE_SEARCHLIGHT_TRIGGER)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DISCOVERED_IMAGO_PASSAGE_EVENT_TRIGGER)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_FAKE_POWER_BOMB)] = sFakePowerBombPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE_CARRYING_POWER_BOMB)] = sSpacePiratePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_RED_GARUTA)] = sTangleVinePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_GERUTA)] = sTangleVinePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_LARVA_RIGHT)] = sTangleVinePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_LARVA_LEFT)] = sTangleVinePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_WATER_DROP)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_FALLING_CHOZO_PILLAR)] = sFallingChozoPillarPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MECHA_RIDLEY)] = sMechaRidleyPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_EXPLOSION_ZEBES_ESCAPE)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_LARGE_DIAGONAL_UP)] = sSteamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_SMALL_DIAGONAL_UP)] = sSteamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_LARGE_DIAGONAL_DOWN)] = sSteamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_SMALL_DIAGONAL_DOWN)] = sSteamPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_BARISTUTE_KRAID_UPPER)] = sBaristutePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ESCAPE_GATE1)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ESCAPE_GATE2)] = sZeelaPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_BLACK_SPACE_PIRATE)] = sSpacePiratePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ESCAPE_SHIP_SPACE_PIRATE)] = sSpacePiratePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_BARISTUTE_KRAID_LOWER)] = sBaristutePal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN2)] = sRinkaZebetiteAndCannonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN3)] = sRinkaZebetiteAndCannonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN4)] = sRinkaZebetiteAndCannonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN5)] = sRinkaZebetiteAndCannonPal;
    sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN6)] = sRinkaZebetiteAndCannonPal;
}
#else
static const u16* sSpritesPalettePointers[PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_COUNT)] = {
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_UNUSED16)] = sUnusedSpritesPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MESSAGE_BANNER)] = sMessageBannerPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZOOMER_YELLOW)] = sZoomerPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZOOMER_RED)] = sZoomerPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEELA)] = sZeelaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEELA_RED)] = sZeelaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIPPER_BROWN)] = sRipperBrownPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIPPER_PURPLE)] = sRipperPurplePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEB)] = sZebPinkPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEB_BLUE)] = sZebBluePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_LARGE_ENERGY_DROP)] = sZeelaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SMALL_ENERGY_DROP)] = sZeelaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MISSILE_DROP)] = sZeelaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SUPER_MISSILE_DROP)] = sZeelaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_POWER_BOMB_DROP)] = sZeelaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SKREE_GREEN)] = sSkreeGreenPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SKREE_BLUE)] = sSkreeBluePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MORPH_BALL)] = sMorphBallPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_LONG_HINT)] = sChozoStatueLongBeamPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_LONG)] = sChozoStatueLongBeamPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_ICE_HINT)] = sChozoStatueIceBeamPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_ICE)] = sChozoStatueIceBeamPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_WAVE_HINT)] = sChozoStatueWaveBeamPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_WAVE)] = sChozoStatueWaveBeamPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_BOMB_HINT)] = sChozoStatueBombsPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_BOMB)] = sChozoStatueBombsPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_SPEEDBOOSTER_HINT)] = sChozoStatueSpeedboosterPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_SPEEDBOOSTER)] = sChozoStatueSpeedboosterPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_HIGH_JUMP_HINT)] = sChozoStatueHighJumpPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_HIGH_JUMP)] = sChozoStatueHighJumpPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_SCREW_HINT)] = sChozoStatueScrewAttackPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_SCREW)] = sChozoStatueScrewAttackPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_VARIA_HINT)] = sChozoStatueVariaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_VARIA)] = sChozoStatueVariaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SOVA_PURPLE)] = sSovaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SOVA_ORANGE)] = sSovaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MULTIVIOLA)] = sMultiviolaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MULTIPLE_LARGE_ENERGY)] = sZeelaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERUTA_RED)] = sGerutaRedPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERUTA_GREEN)] = sGerutaGreenPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SQUEEPT)] = sSqueeptPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SQUEEPT_UNUSED)] = sSqueeptPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MAP_STATION)] = sMapStationPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DRAGON)] = sDragonPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DRAGON_UNUSED)] = sDragonPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZIPLINE)] = sZiplinePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZIPLINE_BUTTON)] = sZiplinePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_REO_GREEN_WINGS)] = sReoGreenWingsPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_REO_PURPLE_WINGS)] = sReoPurpleWingsPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GUNSHIP)] = sGunshipPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DEOREM_FIRST_LOCATION)] = sDeoremPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DEOREM_SECOND_LOCATION)] = sDeoremPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHARGE_BEAM)] = sChargeBeamPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SKULTERA)] = sSkulteraPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DESSGEEGA)] = sDessgeegaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DESSGEEGA_AFTER_LONG_BEAM)] = sDessgeegaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_WAVER)] = sWaverPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_WAVER_UNUSED)] = sWaverPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MELLOW)] = sHivePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_HIVE)] = sHivePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_POWER_GRIP)] = sPowerGripPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO_LARVA_RIGHT)] = sImagoLarvaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MORPH_BALL_LAUNCHER)] = sMorphBallLauncherPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO_COCOON)] = sImagoCocoonPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ELEVATOR_PAD)] = sElevatorPadPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE)] = sSpacePiratePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE_WAITING1)] = sSpacePiratePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE_WAITING2)] = sSpacePiratePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE_WAITING3)] = sSpacePiratePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE2)] = sSpacePiratePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GAMET_BLUE_SINGLE)] = sGametBluePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GAMET_RED_SINGLE)] = sGametRedPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_GRAVITY)] = sChozoStatueGravitySuitPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_SPACE_JUMP)] = sChozoStatueSpaceJumpPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_GATE_DEFAULT_OPEN)] = sSecurityGatePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBBO_GREEN)] = sZebboGreenPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBBO_YELLOW)] = sZebboYellowPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_WORKER_ROBOT)] = sWorkerRobotPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_PARASITE_MULTIPLE)] = sParasitePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_PARASITE)] = sParasitePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_PISTON)] = sPistonPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIDLEY)] = sRidleyPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_GATE_DEFAULT_CLOSED)] = sSecurityGatePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZIPLINE_GENERATOR)] = sZiplineGeneratorPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_METROID)] = sMetroidPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_FROZEN_METROID)] = sMetroidPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_ORANGE)] = sRinkaOrangePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_POLYP)] = sPolypPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_VIOLA_BLUE)] = sViolaBluePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_VIOLA_ORANGE)] = sViolaOrangePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_NORFAIR)] = sGeronNorfairPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_HOLTZ)] = sHoltzPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GEKITAI_MACHINE)] = sGekitaiMachinePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RUINS_TEST)] = sRuinsTestPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SAVE_PLATFORM)] = sSavePlatformPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_KRAID)] = sKraidPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO_COCOON_AFTER_FIGHT)] = sImagoCocoonPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIPPERII)] = sRipper2Pal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MELLA)] = sMellaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ATOMIC)] = sAtomicPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_AREA_BANNER)] = sAreaBannerPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MOTHER_BRAIN)] = sMotherBrainPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_FAKE_POWER_BOMB_EVENT_TRIGGER)] = sZeelaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ACID_WORM)] = sAcidWormPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ESCAPE_SHIP)] = sEscapeShipPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SIDEHOPPER)] = sSidehopperPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GEEGA)] = sGeegaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GEEGA_WHITE)] = sGeegaWhitePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN)] = sRinkaZebetiteAndCannonPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBETITE_ONE_AND_THREE)] = sRinkaZebetiteAndCannonPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CANNON)] = sRinkaZebetiteAndCannonPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO_LARVA_RIGHT_SIDE)] = sImagoLarvaRightSidePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_TALL)] = sTangleVinePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_MEDIUM)] = sTangleVinePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_CURVED)] = sTangleVinePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_SHORT)] = sTangleVinePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MELLOW_SWARM)] = sHivePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MELLOW_SWARM_HEALTH_BASED)] = sHivePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO)] = sImagoPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBETITE_TWO_AND_FOUR)] = sRinkaZebetiteAndCannonPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CANNON2)] = sRinkaZebetiteAndCannonPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CANNON3)] = sRinkaZebetiteAndCannonPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CROCOMIRE)] = sCrocomirePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_IMAGO_LARVA_LEFT)] = sImagoLarvaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_BRINSTAR_ROOM_15)] = sGeronPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_BRINSTAR_ROOM_1C)] = sGeronPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_VARIA1)] = sGeronPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_VARIA2)] = sGeronPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GERON_VARIA3)] = sGeronPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GLASS_TUBE)] = sGlassTubePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SAVE_PLATFORM_CHOZODIA)] = sSavePlatformChozodiaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_BARISTUTE)] = sBaristutePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_CHOZO_STATUE_PLASMA_BEAM)] = sChozoStatuePlasmaBeamPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_KRAID_ELEVATOR_STATUE)] = sElevatorStatuesPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIDLEY_ELEVATOR_STATUE)] = sElevatorStatuesPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RISING_CHOZO_PILLAR)] = sRisingChozoPillarPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_LASER_VERTICAL)] = sSecurityLaserPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_LASER_HORIZONTAL)] = sSecurityLaserPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_LASER_VERTICAL2)] = sSecurityLaserPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SECURITY_LASER_HORIZONTAL2)] = sSecurityLaserPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_LOCK_UNLOCK_METROID_DOORS_UNUSED)] = sZeelaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GAMET_BLUE_LEADER)] = sGametBluePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GAMET_BLUE_FOLLOWER)] = sGametBluePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GEEGA_LEADER)] = sGeegaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GEEGA_FOLLOWER)] = sGeegaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBBO_GREEN_LEADER)] = sZebboGreenPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ZEBBO_GREEN_FOLLOWER)] = sZebboGreenPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_KRAID_STATUE)] = sBossStatuesPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RIDLEY_STATUE)] = sBossStatuesPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_GREEN)] = sRinkaGreenPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT_EYE)] = sSearchlightEyePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT_EYE2)] = sSearchlightEyePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_LARGE)] = sSteamPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_SMALL)] = sSteamPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_PLASMA_BEAM_BLOCK)] = sPlasmaBeamBlockPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GRAVITY_SUIT_BLOCK)] = sGravityBlockPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_JUMP_BLOCK)] = sSpaceJumpBlockPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GADORA_KRAID)] = sGadoraPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_GADORA_RIDLEY)] = sGadoraPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT)] = sSearchlightPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT2)] = sSearchlightPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT3)] = sSearchlightPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SEARCHLIGHT4)] = sSearchlightPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MAYBE_SEARCHLIGHT_TRIGGER)] = sZeelaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_DISCOVERED_IMAGO_PASSAGE_EVENT_TRIGGER)] = sZeelaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_FAKE_POWER_BOMB)] = sFakePowerBombPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_SPACE_PIRATE_CARRYING_POWER_BOMB)] = sSpacePiratePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_RED_GARUTA)] = sTangleVinePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_GERUTA)] = sTangleVinePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_LARVA_RIGHT)] = sTangleVinePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_TANGLE_VINE_LARVA_LEFT)] = sTangleVinePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_WATER_DROP)] = sZeelaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_FALLING_CHOZO_PILLAR)] = sFallingChozoPillarPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_MECHA_RIDLEY)] = sMechaRidleyPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_EXPLOSION_ZEBES_ESCAPE)] = sZeelaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_LARGE_DIAGONAL_UP)] = sSteamPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_SMALL_DIAGONAL_UP)] = sSteamPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_LARGE_DIAGONAL_DOWN)] = sSteamPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_STEAM_SMALL_DIAGONAL_DOWN)] = sSteamPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_BARISTUTE_KRAID_UPPER)] = sBaristutePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ESCAPE_GATE1)] = sZeelaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ESCAPE_GATE2)] = sZeelaPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_BLACK_SPACE_PIRATE)] = sSpacePiratePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_ESCAPE_SHIP_SPACE_PIRATE)] = sSpacePiratePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_BARISTUTE_KRAID_LOWER)] = sBaristutePal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN2)] = sRinkaZebetiteAndCannonPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN3)] = sRinkaZebetiteAndCannonPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN4)] = sRinkaZebetiteAndCannonPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN5)] = sRinkaZebetiteAndCannonPal,
    [PSPRITE_OFFSET_FOR_GRAPHICS(PSPRITE_RINKA_MOTHER_BRAIN6)] = sRinkaZebetiteAndCannonPal
};
#endif
#endif

static Func_T sSecondarySpritesAIPointers[SSPRITE_COUNT] = {
    [SSPRITE_CHOZO_BALL] = ChozoBall,
    [SSPRITE_CHOZO_STATUE_PART] = ChozoStatuePart,
    [SSPRITE_CHOZO_STATUE_REFILL] = ChozoStatueRefill,
    [SSPRITE_KRAID_PART] = KraidPart,
    [SSPRITE_CHOZO_STATUE_MOVEMENT] = ChozoStatueMovement,
    [SSPRITE_CHARGE_BEAM_GLOW] = ChargeBeamGlow,
    [SSPRITE_WINGED_RIPPER] = WingedRipper,
    [SSPRITE_MULTIVIOLA_UNUSED] = MultiviolaUnused,
    [SSPRITE_DRAGON_FIREBALL] = DragonFireball,
    [SSPRITE_DEOREM_SEGMENT] = DeoremSegment,
    [SSPRITE_DEOREM_EYE] = DeoremEye,
    [SSPRITE_DEOREM_THORN] = DeoremThorn,
    [SSPRITE_SKREE_EXPLOSION] = SkreeExplosion,
    [SSPRITE_SAVE_PLATFORM_PART] = SavePlatformPart,
    [SSPRITE_SAVE_YES_NO_CURSOR] = SaveYesNoCursor,
    [SSPRITE_BLUE_SKREE_EXPLOSION] = SkreeExplosion,
    [SSPRITE_ZEELA_EYES] = ZeelaEyes,
    [SSPRITE_HIVE_ROOTS] = HiveRoots,
    [SSPRITE_IMAGO_LARVA_PART] = ImagoLarvaPart,
    [SSPRITE_MORPH_BALL_OUTSIDE] = MorphBallOutside,
    [SSPRITE_IMAGO_COCOON_VINE] = ImagoCocoonVine,
    [SSPRITE_IMAGO_COCOON_SPORE] = ImagoCocoonSpore,
    [SSPRITE_SPACE_PIRATE_LASER] = SpacePirateLaser,
    [SSPRITE_RIDLEY_PART] = RidleyPart,
    [SSPRITE_RIDLEY_TAIL] = RidleyTail,
    [SSPRITE_SEARCHLIGHT_EYE_BEAM] = SearchlightEyeBeam,
    [SSPRITE_METROID_SHELL] = MetroidShell,
    [SSPRITE_POLYP_PROJECTILE] = PolypProjectile,
    [SSPRITE_KRAID_SPIKE] = KraidSpike,
    [SSPRITE_KRAID_NAIL] = KraidNail,
    [SSPRITE_ZIPLINE_GENERATOR_PART] = ZiplineGeneratorPart,
    [SSPRITE_ATOMIC_ELECTRICITY] = AtomicElectricity,
    [SSPRITE_MOTHER_BRAIN_PART] = MotherBrainPart,
    [SSPRITE_RIDLEY_FIREBALL] = RidleyFireball,
    [SSPRITE_UNKNOWN_ITEM_CHOZO_STATUE_PART] = UnknownItemChozoStatuePart,
    [SSPRITE_UNKNOWN_ITEM_CHOZO_STATUE_REFILL] = UnknownItemChozoStatueRefill,
    [SSPRITE_MORPH_BALL_LAUNCHER_PART] = MorphBallLauncherPart,
    [SSPRITE_ACID_WORM_PART] = AcidWormPart,
    [SSPRITE_ACID_WORM_SPIT] = AcidWormSpit,
    [SSPRITE_CANNON_BULLET] = CannonBullet,
    [SSPRITE_CROCOMIRE_PART] = CrocomirePart,
    [SSPRITE_IMAGO_PART] = ImagoPart,
    [SSPRITE_DEFEATED_IMAGO_COCOON] = DefeatedImagoCocoon,
    [SSPRITE_IMAGO_CEILING_VINE] = ImagoCocoonCeilingVine,
    [SSPRITE_SEARCHLIGHT_EYE_BEAM2] = SearchlightEyeBeam,
    [SSPRITE_TANGLE_VINE_GERUTA_PART] = TangleVineGerutaPart,
    [SSPRITE_CHOZODIA_SAVE_PLATFORM_PART] = SavePlatformChozodiaPart,
    [SSPRITE_IMAGO_NEEDLE] = ImagoNeedle,
    [SSPRITE_ELEVATOR_STATUE_DEBRIS] = ElevatorStatueDebris,
    [SSPRITE_IMAGO_DAMAGED_STINGER] = ImagoDamagedStinger,
    [SSPRITE_GUNSHIP_PART] = GunshipPart,
    [SSPRITE_IMAGO_EGG] = ImagoEgg,
    [SSPRITE_MAP_STATION_PART] = MapStationPart,
    [SSPRITE_CHOZO_PILLAR_PLATFORM] = ChozoPillarPlatform,
    [SSPRITE_GADORA_EYE] = GadoraEye,
    [SSPRITE_GADORA_BEAM] = GadoraBeam,
    [SSPRITE_UNKNOWN_ITEM_BLOCK_LIGHT] = UnknownItemBlockLight,
    [SSPRITE_SEARCHLIGHT_EYE_PROJECTILE] = SearchlightEyeProjectile,
    [SSPRITE_CHOZO_PILLAR_PLATFORM_SHADOW] = ChozoPillarPlatformShadow,
    [SSPRITE_RUINS_TEST_SYMBOL] = RuinsTestSymbol,
    [SSPRITE_RUINS_TEST_SAMUS_REFLECTION_START] = RuinsTestSamusReflectionStart,
    [SSPRITE_RUINS_TEST_REFLECTION_COVER] = RuinsTestReflectionCover,
    [SSPRITE_RUINS_TEST_GHOST_OUTLINE] = RuinsTestGhostOutline,
    [SSPRITE_RUINS_TEST_GHOST] = RuinsTestGhost,
    [SSPRITE_RUINS_TEST_SHOOTABLE_SYMBOL] = RuinsTestShootableSymbol,
    [SSPRITE_RUINS_TEST_SAMUS_REFLECTION_END] = RuinsTestSamusReflectionEnd,
    [SSPRITE_RUINS_TEST_LIGHTNING] = RuinsTestLightning,
    [SSPRITE_RIDLEY_BIG_FIREBALL] = RidleyFireball,
    [SSPRITE_MECHA_RIDLEY_PART] = MechaRidleyPart,
    [SSPRITE_ESCAPE_SHIP_PART] = EscapeShipPart,
    [SSPRITE_POWER_GRIP_GLOW] = PowerGripGlow,
    [SSPRITE_MECHA_RIDLEY_LASER] = MechaRidleyLaser,
    [SSPRITE_MECHA_RIDLEY_MISSILE] = MechaRidleyMissile,
    [SSPRITE_MECHA_RIDLEY_FIREBALL] = MechaRidleyFireball,
    [SSPRITE_MOTHER_BRAIN_BEAM] = MotherBrainBeam,
    [SSPRITE_MOTHER_BRAIN_BLOCK] = MotherBrainBlock,
    [SSPRITE_MOTHER_BRAIN_GLASS_BREAKING] = MotherBrainGlassBreaking 
};

#ifdef MZM_3DS
static const u8* sSpritesetPointers[MAX_AMOUNT_OF_SPRITESET];
void Init_sSpritesetPointers(void) {
    sSpritesetPointers[0] = sSpriteset0;
    sSpritesetPointers[1] = sSpriteset1;
    sSpritesetPointers[2] = sSpriteset2;
    sSpritesetPointers[3] = sSpriteset3;
    sSpritesetPointers[4] = sSpriteset4;
    sSpritesetPointers[5] = sSpriteset5;
    sSpritesetPointers[6] = sSpriteset6;
    sSpritesetPointers[7] = sSpriteset7;
    sSpritesetPointers[8] = sSpriteset8;
    sSpritesetPointers[9] = sSpriteset9;
    sSpritesetPointers[10] = sSpriteset10;
    sSpritesetPointers[11] = sSpriteset11;
    sSpritesetPointers[12] = sSpriteset12;
    sSpritesetPointers[13] = sSpriteset13;
    sSpritesetPointers[14] = sSpriteset14;
    sSpritesetPointers[15] = sSpriteset15;
    sSpritesetPointers[16] = sSpriteset16;
    sSpritesetPointers[17] = sSpriteset17;
    sSpritesetPointers[18] = sSpriteset18;
    sSpritesetPointers[19] = sSpriteset19;
    sSpritesetPointers[20] = sSpriteset20;
    sSpritesetPointers[21] = sSpriteset21;
    sSpritesetPointers[22] = sSpriteset22;
    sSpritesetPointers[23] = sSpriteset23;
    sSpritesetPointers[24] = sSpriteset24;
    sSpritesetPointers[25] = sSpriteset25;
    sSpritesetPointers[26] = sSpriteset26;
    sSpritesetPointers[27] = sSpriteset27;
    sSpritesetPointers[28] = sSpriteset28;
    sSpritesetPointers[29] = sSpriteset29;
    sSpritesetPointers[30] = sSpriteset30;
    sSpritesetPointers[31] = sSpriteset31;
    sSpritesetPointers[32] = sSpriteset32;
    sSpritesetPointers[33] = sSpriteset33;
    sSpritesetPointers[34] = sSpriteset34;
    sSpritesetPointers[35] = sSpriteset35;
    sSpritesetPointers[36] = sSpriteset36;
    sSpritesetPointers[37] = sSpriteset37;
    sSpritesetPointers[38] = sSpriteset38;
    sSpritesetPointers[39] = sSpriteset39;
    sSpritesetPointers[40] = sSpriteset40;
    sSpritesetPointers[41] = sSpriteset41;
    sSpritesetPointers[42] = sSpriteset42;
    sSpritesetPointers[43] = sSpriteset43;
    sSpritesetPointers[44] = sSpriteset44;
    sSpritesetPointers[45] = sSpriteset45;
    sSpritesetPointers[46] = sSpriteset46;
    sSpritesetPointers[47] = sSpriteset47;
    sSpritesetPointers[48] = sSpriteset48;
    sSpritesetPointers[49] = sSpriteset49;
    sSpritesetPointers[50] = sSpriteset50;
    sSpritesetPointers[51] = sSpriteset51;
    sSpritesetPointers[52] = sSpriteset52;
    sSpritesetPointers[53] = sSpriteset53;
    sSpritesetPointers[54] = sSpriteset54;
    sSpritesetPointers[55] = sSpriteset55;
    sSpritesetPointers[56] = sSpriteset56;
    sSpritesetPointers[57] = sSpriteset57;
    sSpritesetPointers[58] = sSpriteset58;
    sSpritesetPointers[59] = sSpriteset59;
    sSpritesetPointers[60] = sSpriteset60;
    sSpritesetPointers[61] = sSpriteset61;
    sSpritesetPointers[62] = sSpriteset62;
    sSpritesetPointers[63] = sSpriteset63;
    sSpritesetPointers[64] = sSpriteset64;
    sSpritesetPointers[65] = sSpriteset65;
    sSpritesetPointers[66] = sSpriteset66;
    sSpritesetPointers[67] = sSpriteset67;
    sSpritesetPointers[68] = sSpriteset68;
    sSpritesetPointers[69] = sSpriteset69;
    sSpritesetPointers[70] = sSpriteset70;
    sSpritesetPointers[71] = sSpriteset71;
    sSpritesetPointers[72] = sSpriteset72;
    sSpritesetPointers[73] = sSpriteset73;
    sSpritesetPointers[74] = sSpriteset74;
    sSpritesetPointers[75] = sSpriteset75;
    sSpritesetPointers[76] = sSpriteset76;
    sSpritesetPointers[77] = sSpriteset77;
    sSpritesetPointers[78] = sSpriteset78;
    sSpritesetPointers[79] = sSpriteset79;
    sSpritesetPointers[80] = sSpriteset80;
    sSpritesetPointers[81] = sSpriteset81;
    sSpritesetPointers[82] = sSpriteset82;
    sSpritesetPointers[83] = sSpriteset83;
    sSpritesetPointers[84] = sSpriteset84;
    sSpritesetPointers[85] = sSpriteset85;
    sSpritesetPointers[86] = sSpriteset86;
    sSpritesetPointers[87] = sSpriteset87;
    sSpritesetPointers[88] = sSpriteset88;
    sSpritesetPointers[89] = sSpriteset89;
    sSpritesetPointers[90] = sSpriteset90;
    sSpritesetPointers[91] = sSpriteset91;
    sSpritesetPointers[92] = sSpriteset92;
    sSpritesetPointers[93] = sSpriteset93;
    sSpritesetPointers[94] = sSpriteset94;
    sSpritesetPointers[95] = sSpriteset95;
    sSpritesetPointers[96] = sSpriteset96;
    sSpritesetPointers[97] = sSpriteset97;
    sSpritesetPointers[98] = sSpriteset98;
    sSpritesetPointers[99] = sSpriteset99;
    sSpritesetPointers[100] = sSpriteset100;
    sSpritesetPointers[101] = sSpriteset101;
    sSpritesetPointers[102] = sSpriteset102;
    sSpritesetPointers[103] = sSpriteset103;
    sSpritesetPointers[104] = sSpriteset104;
    sSpritesetPointers[105] = sSpriteset105;
    sSpritesetPointers[106] = sSpriteset106;
    sSpritesetPointers[107] = sSpriteset107;
    sSpritesetPointers[108] = sSpriteset108;
    sSpritesetPointers[109] = sSpriteset109;
    sSpritesetPointers[110] = sSpriteset110;
    sSpritesetPointers[111] = sSpriteset111;
    sSpritesetPointers[112] = sSpriteset112;
    sSpritesetPointers[113] = sSpriteset113;
}
#else
static const u8* sSpritesetPointers[MAX_AMOUNT_OF_SPRITESET] = {
    sSpriteset0,
    sSpriteset1,
    sSpriteset2,
    sSpriteset3,
    sSpriteset4,
    sSpriteset5,
    sSpriteset6,
    sSpriteset7,
    sSpriteset8,
    sSpriteset9,
    sSpriteset10,
    sSpriteset11,
    sSpriteset12,
    sSpriteset13,
    sSpriteset14,
    sSpriteset15,
    sSpriteset16,
    sSpriteset17,
    sSpriteset18,
    sSpriteset19,
    sSpriteset20,
    sSpriteset21,
    sSpriteset22,
    sSpriteset23,
    sSpriteset24,
    sSpriteset25,
    sSpriteset26,
    sSpriteset27,
    sSpriteset28,
    sSpriteset29,
    sSpriteset30,
    sSpriteset31,
    sSpriteset32,
    sSpriteset33,
    sSpriteset34,
    sSpriteset35,
    sSpriteset36,
    sSpriteset37,
    sSpriteset38,
    sSpriteset39,
    sSpriteset40,
    sSpriteset41,
    sSpriteset42,
    sSpriteset43,
    sSpriteset44,
    sSpriteset45,
    sSpriteset46,
    sSpriteset47,
    sSpriteset48,
    sSpriteset49,
    sSpriteset50,
    sSpriteset51,
    sSpriteset52,
    sSpriteset53,
    sSpriteset54,
    sSpriteset55,
    sSpriteset56,
    sSpriteset57,
    sSpriteset58,
    sSpriteset59,
    sSpriteset60,
    sSpriteset61,
    sSpriteset62,
    sSpriteset63,
    sSpriteset64,
    sSpriteset65,
    sSpriteset66,
    sSpriteset67,
    sSpriteset68,
    sSpriteset69,
    sSpriteset70,
    sSpriteset71,
    sSpriteset72,
    sSpriteset73,
    sSpriteset74,
    sSpriteset75,
    sSpriteset76,
    sSpriteset77,
    sSpriteset78,
    sSpriteset79,
    sSpriteset80,
    sSpriteset81,
    sSpriteset82,
    sSpriteset83,
    sSpriteset84,
    sSpriteset85,
    sSpriteset86,
    sSpriteset87,
    sSpriteset88,
    sSpriteset89,
    sSpriteset90,
    sSpriteset91,
    sSpriteset92,
    sSpriteset93,
    sSpriteset94,
    sSpriteset95,
    sSpriteset96,
    sSpriteset97,
    sSpriteset98,
    sSpriteset99,
    sSpriteset100,
    sSpriteset101,
    sSpriteset102,
    sSpriteset103,
    sSpriteset104,
    sSpriteset105,
    sSpriteset106,
    sSpriteset107,
    sSpriteset108,
    sSpriteset109,
    sSpriteset110,
    sSpriteset111,
    sSpriteset112,
    sSpriteset113
};
#endif

/**
 * cf00 | 42c | Main routine that updates all the sprites
 * 
 */
void SpriteUpdate(void)
{
    u16 rngParam1;
    u16 rngParam2;
    u8 count;
    struct SpriteData* pCurrent;

    pCurrent = &gCurrentSprite;
    rngParam1 = gFrameCounter8Bit;
    rngParam2 = gFrameCounter16Bit / 16;

    if (gSubGameMode1 == SUB_GAME_MODE_PLAYING)
    {
        // In normal gameplay

        SpriteDebrisProcessAll();

        if (!SpriteUtilCheckStopSpritesPose())
        {
            // Samus is able, update sprites normally

            // Check collision
            SpriteUtilSamusAndSpriteCollision();

            for (count = 0; count < MAX_AMOUNT_OF_SPRITES; count++)
            {
                if (!(gSpriteData[count].status & SPRITE_STATUS_EXISTS))
                    continue;

                // Transfer sprite to current
                DMA3_COPY_16(&gSpriteData[count], &gCurrentSprite, sizeof(struct SpriteData) / 2);

                // Update random number
                gSpriteRng = ARRAY_ACCESS(sSpriteRandomNumberTable, rngParam1 + count + rngParam2 + pCurrent->xPosition + pCurrent->yPosition);

                // Update stun timer
                SpriteUtilUpdateStunTimer(pCurrent);

                // Call AI
                if (pCurrent->properties & SP_SECONDARY_SPRITE)
                    sSecondarySpritesAIPointers[pCurrent->spriteId]();
                else
                    sPrimarySpritesAIPointers[pCurrent->spriteId]();

                // Check update sprite info if still alive
                if (pCurrent->status & SPRITE_STATUS_EXISTS)
                {
                    SpriteUtilSamusStandingOnSprite(pCurrent);
                    SpriteUpdateAnimation(pCurrent);
                    SpriteCheckOnScreen(pCurrent);
                }

                // Transfer current back to array
                DMA3_COPY_16(&gCurrentSprite, &gSpriteData[count], sizeof(struct SpriteData) / 2);
            }

            // Update alarm
            DecrementChozodiaAlarm();

            if (gParasiteRelated != 0)
                gParasiteRelated--;
        }
        else
        {
            // Samus isn't able, still update sprites but only on some conditions

            for (count = 0; count < MAX_AMOUNT_OF_SPRITES; count++)
            {
                if (!(gSpriteData[count].status & SPRITE_STATUS_EXISTS))
                    continue;

                if (gSpriteData[count].pose == SPRITE_POSE_UNINITIALIZED || gSpriteData[count].properties & SP_ALWAYS_ACTIVE)
                {
                    // Only update sprites to initialize them or if they have the always active flag

                    // Transfer sprite to current
                    DMA3_COPY_16(&gSpriteData[count], &gCurrentSprite, sizeof(struct SpriteData) / 2);
                    
                    // Update random number
                    gSpriteRng = ARRAY_ACCESS(sSpriteRandomNumberTable, rngParam1 + count + rngParam2 + pCurrent->xPosition + pCurrent->yPosition);

                    // Update stun timer
                    SpriteUtilUpdateStunTimer(pCurrent);

                    // Call AI
                    if (pCurrent->properties & SP_SECONDARY_SPRITE)
                        sSecondarySpritesAIPointers[pCurrent->spriteId]();
                    else
                        sPrimarySpritesAIPointers[pCurrent->spriteId]();

                    // Check update sprite info if still alive
                    if (pCurrent->status & SPRITE_STATUS_EXISTS)
                    {
                        SpriteUtilSamusStandingOnSprite(pCurrent);
                        SpriteUpdateAnimation(pCurrent);
                        SpriteCheckOnScreen(pCurrent);
                    }

                    // Transfer current back to array
                    DMA3_COPY_16(&gCurrentSprite, &gSpriteData[count], sizeof(struct SpriteData) / 2);
                }
                else
                {
                    // Only check if on screen

                    // Transfer sprite to current
                    DMA3_COPY_16(&gSpriteData[count], &gCurrentSprite, sizeof(struct SpriteData) / 2);

                    SpriteCheckOnScreen(pCurrent);

                    // Transfer current back to array
                    DMA3_COPY_16(&gCurrentSprite, &gSpriteData[count], sizeof(struct SpriteData) / 2);
                }
            }
        }
    }
    else if (gSubGameMode1 == SUB_GAME_MODE_NO_CLIP)
    {
        // In debug no-clip, update sprites normally but don't check for collision
        for (count = 0; count < MAX_AMOUNT_OF_SPRITES; count++)
        {
            if (!(gSpriteData[count].status & SPRITE_STATUS_EXISTS))
                continue;

            // Transfer sprite to current
            DMA3_COPY_16(&gSpriteData[count], &gCurrentSprite, sizeof(struct SpriteData) / 2);

            // Update random number
            gSpriteRng = ARRAY_ACCESS(sSpriteRandomNumberTable, rngParam1 + count + rngParam2 + pCurrent->xPosition + pCurrent->yPosition);
            
            // Update stun timer
            SpriteUtilUpdateStunTimer(pCurrent);

            // Call AI
            if (pCurrent->properties & SP_SECONDARY_SPRITE)
                sSecondarySpritesAIPointers[pCurrent->spriteId]();
            else
                sPrimarySpritesAIPointers[pCurrent->spriteId]();

            // Check update sprite info if still alive
            if (pCurrent->status & SPRITE_STATUS_EXISTS)
            {
                SpriteUtilSamusStandingOnSprite(pCurrent);
                SpriteUpdateAnimation(pCurrent);
                SpriteCheckOnScreen(pCurrent);
            }

            // Transfer current back to array
            DMA3_COPY_16(&gCurrentSprite, &gSpriteData[count], sizeof(struct SpriteData) / 2);
        }

        // Update alarm
        DecrementChozodiaAlarm();

        if (gParasiteRelated != 0)
            APPLY_DELTA_TIME_DEC(gParasiteRelated);
    }
    else
    {
        // Any other sub game mode
        for (count = 0; count < MAX_AMOUNT_OF_SPRITES; count++)
        {
            if (!(gSpriteData[count].status & SPRITE_STATUS_EXISTS))
                continue;

            // Transfer sprite to current
            DMA3_COPY_16(&gSpriteData[count], &gCurrentSprite, sizeof(struct SpriteData) / 2);

            // Update random number
            gSpriteRng = ARRAY_ACCESS(sSpriteRandomNumberTable, rngParam1 + count + rngParam2 + pCurrent->xPosition + pCurrent->yPosition);

            // Only call init code
            if (pCurrent->pose == SPRITE_POSE_UNINITIALIZED)
            {
                // Call AI
                if (pCurrent->properties & SP_SECONDARY_SPRITE)
                    sSecondarySpritesAIPointers[pCurrent->spriteId]();
                else
                    sPrimarySpritesAIPointers[pCurrent->spriteId]();
            }

            // Check update sprite info if still alive
            if (pCurrent->status & SPRITE_STATUS_EXISTS)
                SpriteCheckOnScreen(pCurrent);

            // Transfer current back to array
            DMA3_COPY_16(&gCurrentSprite, &gSpriteData[count], sizeof(struct SpriteData) / 2);
        }
    }
}

/**
 * d32c | 40 | Updates the animation related info of a sprite
 * 
 * @param pSprite Sprite data pointer
 */
void SpriteUpdateAnimation(struct SpriteData* pSprite)
{
    // Don't update the animation if freezed
    if (pSprite->freezeTimer != 0)
        return;

    // Update adc
    APPLY_DELTA_TIME_INC(pSprite->animationDurationCounter);

    // Check reached the end of the current frame
    if (pSprite->pOam[pSprite->currentAnimationFrame].timer < pSprite->animationDurationCounter)
    {
        // Advance to next frame
        pSprite->animationDurationCounter = 1;
        pSprite->currentAnimationFrame++;

        // Reached the end, set caf to 0 to loop back the animation
        if (pSprite->pOam[pSprite->currentAnimationFrame].timer == 0)
            pSprite->currentAnimationFrame = 0;
    }
}

/**
 * @brief d36c | c4 | Draws all high-priority sprites based on the draw order
 * 
 */
void SpriteDrawAll_HighPriority(void)
{
#ifdef PLATFORM_SWITCH
    extern void Port_OverlayText_BeginFrame(void);
    extern void SwitchWorld_BeginFrame(void);
    SwitchWorld_BeginFrame();
    Port_OverlayText_BeginFrame();
#endif

    struct SpriteData* pSprite;
    s32 i;
    s32 drawOrder;
    u32 drawStatus;
    u32 checkStatus;
    u32 notPlaying;

#ifdef __3DS__
    /* First sprite pass of the frame: reset the per-frame OAM depth tags so
     * they only ever describe this frame's sprites (see
     * port_overlay_text_oam.c / port_sprite_depth_oam.c). */
    {
        extern void Port_OverlayText_BeginFrame(void);
        extern void Port_SpriteDepth_BeginFrame(void);
        extern void Port_Wide_BeginFrame(void);
        Port_OverlayText_BeginFrame();
        Port_SpriteDepth_BeginFrame();
        Port_Wide_BeginFrame();
    }
#endif

    if (gSubGameMode1 == SUB_GAME_MODE_PLAYING)
        notPlaying = FALSE;
    else
        notPlaying = TRUE;

    checkStatus = SPRITE_STATUS_EXISTS | SPRITE_STATUS_ONSCREEN | SPRITE_STATUS_NOT_DRAWN | SPRITE_STATUS_HIGH_PRIORITY;
    drawStatus = SPRITE_STATUS_EXISTS | SPRITE_STATUS_ONSCREEN | SPRITE_STATUS_HIGH_PRIORITY;

    for (i = 0; i < MAX_AMOUNT_OF_SPRITES; i++)
    {
        // Sprite doesn't exists or isn't drawn
        if ((gSpriteData[i].status & checkStatus) != drawStatus)
        {
            gSpriteDrawOrder[i] = 0;
            continue;
        }

        // Check is lower draw order
        if (gSpriteData[i].drawOrder >= 9)
        {
            gSpriteDrawOrder[i] = 0;
            continue;
        }

        // Check is playing
        if (!notPlaying)
        {
            gSpriteDrawOrder[i] = gSpriteData[i].drawOrder;
            continue;
        }

        // Don't draw sprites that have an absolute position if not in game
        if (!(gSpriteData[i].properties & SP_ABSOLUTE_POSITION))
            gSpriteDrawOrder[i] = gSpriteData[i].drawOrder;
        else
            gSpriteDrawOrder[i] = 0;
    }

    // Apply draw order and draw sprites
    for (drawOrder = 1; drawOrder < 9; drawOrder++)
    {
        for (i = 0, pSprite = gSpriteData; pSprite < gSpriteData + MAX_AMOUNT_OF_SPRITES; i++, pSprite++)
        {
            if (gSpriteDrawOrder[i] == drawOrder)
                SpriteDraw(pSprite, i);
        }
    }
}

/**
 * @brief d430 | 8c | Draws all medium-priority sprites based on the draw order
 * 
 */
void SpriteDrawAll_MediumPriority(void)
{
    struct SpriteData* pSprite;
    s32 i;
    s32 drawOrder;
    u32 drawStatus;
    u32 checkStatus;

    checkStatus = SPRITE_STATUS_EXISTS | SPRITE_STATUS_ONSCREEN | SPRITE_STATUS_NOT_DRAWN | SPRITE_STATUS_HIGH_PRIORITY;
    drawStatus = SPRITE_STATUS_EXISTS | SPRITE_STATUS_ONSCREEN;
    
    SpriteDebrisDrawAll();

    for (i = 0; i < MAX_AMOUNT_OF_SPRITES; i++)
    {
        // Sprite doesn't exists or isn't drawn
        if ((gSpriteData[i].status & checkStatus) != drawStatus)
        {
            gSpriteDrawOrder[i] = 0;
            continue;
        }

        // Check is lower draw order
        if (gSpriteData[i].drawOrder < 9)
            gSpriteDrawOrder[i] = gSpriteData[i].drawOrder;
        else
            gSpriteDrawOrder[i] = 0;
    }

    // Apply draw order and draw sprites
    for (drawOrder = 1; drawOrder < 9; drawOrder++)
    {
        for (i = 0, pSprite = gSpriteData; pSprite < gSpriteData + MAX_AMOUNT_OF_SPRITES; i++, pSprite++)
        {
            if (gSpriteDrawOrder[i] == drawOrder)
                SpriteDraw(pSprite, i);
        }
    }
}

/**
 * @brief d4bc | 88 | Draws the sprites that have a draw order between 9 and 16
 * 
 */
void SpriteDrawAll_LowPriority(void)
{
    struct SpriteData* pSprite;
    s32 i;
    s32 drawOrder;
    u32 drawStatus;
    u32 checkStatus;

    checkStatus = SPRITE_STATUS_EXISTS | SPRITE_STATUS_ONSCREEN | SPRITE_STATUS_NOT_DRAWN | SPRITE_STATUS_HIGH_PRIORITY;
    drawStatus = SPRITE_STATUS_EXISTS | SPRITE_STATUS_ONSCREEN;

    for (i = 0; i < MAX_AMOUNT_OF_SPRITES; i++)
    {
        // Sprite doesn't exists or isn't drawn
        if ((gSpriteData[i].status & checkStatus) != drawStatus)
        {
            gSpriteDrawOrder[i] = 0;
            continue;
        }

        // Check is upper draw order
        if (gSpriteData[i].drawOrder >= 9)
            gSpriteDrawOrder[i] = gSpriteData[i].drawOrder;
        else
            gSpriteDrawOrder[i] = 0;
    }

    // Apply draw order and draw sprites
    for (drawOrder = 9; drawOrder < 17; drawOrder++)
    {
        for (i = 0, pSprite = gSpriteData; pSprite < gSpriteData + MAX_AMOUNT_OF_SPRITES; i++, pSprite++)
        {
            if (gSpriteDrawOrder[i] == drawOrder)
                SpriteDraw(pSprite, i);
        }
    }
}

/**
 * @brief d544 | 890 | Draws a sprite
 * 
 * @param pSprite Sprite data pointer
 * @param slot Ram slot
 */
void SpriteDraw(struct SpriteData* pSprite, s32 slot)
{
    const u16* src;
    u16* dst;
    u8 prevSlot;
    u16 part1;
    u16 part2;

    u32 shape;
    u32 size;
    
    u16 dy;
    u16 dmy;

    s16 actualY;
    s16 actualX;
    s16 yScaling;
    s16 xScaling;
    s32 y;
    s32 x;
    s32 unk_2;
    s32 unk_3;
    s32 tmpX;
    s32 tmpY;
    s32 scaledX;
    s32 scaledY;

    u16 rotationScalingSingle;
    s32 i;
    u16 partCount;
    
    u32 yOffset;
    u32 xOffset;
    
    u16 xFlip;
    u16 doubleSize;
    u16 alphaBlending;
    u16 mosaic;
    u16 yFlip;
    u32 bgPriority;
    u32 paletteRow;
    u32 gfxOffset;
    u16 xPosition;
    u16 yPosition;
    u16 rotation;
    u16 scaling;

    u8 offset;

    prevSlot = gNextOamSlot;
    src = pSprite->pOam[pSprite->currentAnimationFrame].pFrame;
#if defined(MZM_3DS) || defined(PORT_NATIVE)
    src = GBA_RESOLVE(src);
#endif
    partCount = *src++;


    if (partCount + prevSlot >= OAM_BUFFER_DATA_SIZE)
        return;

    dst = (u16*)(gOamData + prevSlot);
    yPosition = SUB_PIXEL_TO_PIXEL_(pSprite->yPosition) - SUB_PIXEL_TO_PIXEL(gBg1YPosition);
    xPosition = SUB_PIXEL_TO_PIXEL_(pSprite->xPosition) - SUB_PIXEL_TO_PIXEL(gBg1XPosition);

    // Shortcuts for status
    xFlip = pSprite->status & SPRITE_STATUS_X_FLIP;
    rotationScalingSingle = pSprite->status & SPRITE_STATUS_ROTATION_SCALING_SINGLE;
    doubleSize = pSprite->status & SPRITE_STATUS_DOUBLE_SIZE;
    alphaBlending = pSprite->status & SPRITE_STATUS_ALPHA_BLENDING;
    yFlip = pSprite->status & SPRITE_STATUS_Y_FLIP;

    // Get graphical data
    // Palette offset by spriteset slot
    paletteRow = pSprite->spritesetGfxSlot + pSprite->paletteRow;
    // Gfx slot, scale to 2 rows of 8x8 tiles in VRAM
    gfxOffset = pSprite->spritesetGfxSlot * 64;
    bgPriority = pSprite->bgPriority;

    if (gSamusOnTopOfBackgrounds && bgPriority != 0)
        bgPriority--;

    if (pSprite->properties & SP_ABSOLUTE_POSITION)
    {
        yPosition = pSprite->yPosition;
        xPosition = pSprite->xPosition;
    }

    if (!(pSprite->status & SPRITE_STATUS_ROTATION_SCALING_WHOLE))
    {
        for (i = 0; i < partCount; i++)
        {
            // Raw copy
            part1 = *src++;
            *dst++ = part1;
            part2 = *src++;
            *dst++ = part2;
            *dst++ = *src++;

            // Apply position
            gOamData[prevSlot + i].split.y = part1 + yPosition;
            gOamData[prevSlot + i].split.x = part2 + xPosition;

            // Apply graphics
            gOamData[prevSlot + i].split.priority = bgPriority;
            // Add palette row and gfx offset
            gOamData[prevSlot + i].split.paletteNum += paletteRow;
            gOamData[prevSlot + i].split.tileNum += gfxOffset;

            if (xFlip)
            {
                // Enable X flip
                gOamData[prevSlot + i].split.xFlip ^= TRUE;

                shape = gOamData[prevSlot + i].split.shape;
                size = gOamData[prevSlot + i].split.size;
                offset = sOamXFlipOffsets[shape][size];

                // Properly offset x position
                gOamData[prevSlot + i].split.x = xPosition - (part2 + offset * 8);
            }

            if (yFlip)
            {
                // Enable Y flip
                gOamData[prevSlot + i].split.yFlip ^= TRUE;
                shape = gOamData[prevSlot + i].split.shape;
                size = gOamData[prevSlot + i].split.size;
                offset = sOamYFlipOffsets[shape][size];

                // Properly offset x position
                gOamData[prevSlot + i].split.y = yPosition - (part1 + offset * 8);
            }

            // Rotates and scales objects at their centers independently if SS_ROTATE_SCALE_INDIVIDUAL is set
            // Breaks if any of the objects are flipped (not the sprite status)
            if (rotationScalingSingle)
            {
                if (doubleSize)
                {
                    // Rotation scaling and double size
                    gOamData[prevSlot + i].split.affineMode = 3;
                }
                else
                {
                    // Rotation scaling
                    gOamData[prevSlot + i].split.affineMode = 1;
                }

                // In this affine mode, X/Y flip are part of the matrix num, so this is just doing matrixNum = slot
                gOamData[prevSlot + i].split.yFlip = slot >> 4;
                gOamData[prevSlot + i].split.xFlip = slot >> 3;
                gOamData[prevSlot + i].split.matrixNum = slot;
            }

            if (alphaBlending)
            {
                // Semi transparent
                gOamData[prevSlot + i].split.objMode = OAM_OBJ_MODE_SEMI_TRANSPARENT;
            }

            dst++;
        }

        // Update next oam slot
        gNextOamSlot = partCount + prevSlot;

        if (rotationScalingSingle)
        {
            rotation = pSprite->rotation;
            scaling = pSprite->scaling;

            // Rotation matrix (column major mode) :
            // [ cos / scaling, -sin / scaling ]
            // [ sin / scaling,  cos / scaling ]

            // If x flipped, then negate scaling on the first column to manually flip the sprite since flipping
            // isn't supported by hardware when affine transformation is enabled
            if (xFlip)
            {
                gOamData[slot * 4 + 0].all.affineParam = FixedMultiplication(COS(rotation), FixedInverse(-scaling));
                gOamData[slot * 4 + 1].all.affineParam = FixedMultiplication(SIN(rotation), FixedInverse(-scaling));
            }
            else
            {
                gOamData[slot * 4 + 0].all.affineParam = FixedMultiplication(COS(rotation), FixedInverse(scaling));
                gOamData[slot * 4 + 1].all.affineParam = FixedMultiplication(SIN(rotation), FixedInverse(scaling));
            }
            
            gOamData[slot * 4 + 2].all.affineParam = FixedMultiplication(-SIN(rotation), FixedInverse(scaling));
            gOamData[slot * 4 + 3].all.affineParam = FixedMultiplication(COS(rotation), FixedInverse(scaling));
        }
    }
    else
    {
        rotation = pSprite->rotation;
        scaling = pSprite->scaling;

        mosaic = pSprite->status & SPRITE_STATUS_MOSAIC;

        yPosition += BLOCK_SIZE;
        xPosition += BLOCK_SIZE;

        for (i = 0; i < partCount; i++)
        {
            // Raw copy
            part1 = *src++;
            *dst++ = part1;
            part2 = *src++;
            *dst++ = part2;
            *dst++ = *src++;

            // Apply graphics
            gOamData[prevSlot + i].split.priority = bgPriority;
            // Add palette row and gfx offset
            gOamData[prevSlot + i].split.paletteNum += paletteRow;
            gOamData[prevSlot + i].split.tileNum += gfxOffset;

            // Rotates and scales the whole sprite, ignores flip
            shape = gOamData[prevSlot + i].split.shape;
            size = gOamData[prevSlot + i].split.size;
        
            // Get center relative to top-left corner of object
            yOffset = sOamYFlipOffsets[shape][size];
            yOffset = PIXEL_TO_SUB_PIXEL(yOffset);
            xOffset = sOamXFlipOffsets[shape][size];
            xOffset = PIXEL_TO_SUB_PIXEL(xOffset);

            // Get current positions
            y = (s16)MOD_AND(part1 + yPosition, 256);
            x = (s16)MOD_AND(part2 + xPosition, 512);
        
            // Get center of object relative to the sprite's position
            tmpY = (s16)(y - yPosition + yOffset);
            tmpX = (s16)(x - xPosition + xOffset);

            // Apply scaling
            tmpX = (s16)(Q_8_8_TO_S16_DIV(tmpX * scaling) - tmpX);
            tmpY = (s16)(Q_8_8_TO_S16_DIV(tmpY * scaling) - tmpY);
        
            x = (s16)(x + tmpX);
            y = (s16)(y + tmpY);
        
            // Offset to 0;0 temporarly to apply the rotation
            unk_2 = (s16)(x - xPosition + xOffset);
            unk_3 = (s16)(y - yPosition + yOffset);
        
            // Rotation matrix
            x = Q_8_8_TO_S16(unk_2 * COS(rotation) - unk_3 * SIN(rotation));
            y = Q_8_8_TO_S16(unk_2 * SIN(rotation) + unk_3 * COS(rotation));
        
            // Offset it back to top-left corner
            if (doubleSize)
            {
                x = (s16)(x - xOffset * 2);
                y = (s16)(y - yOffset * 2);
            }
            else
            {
                x = (s16)(x - xOffset);
                y = (s16)(y - yOffset);
            }
        
            // Rotated position + position
            gOamData[prevSlot + i].split.y = MOD_AND(y + yPosition - BLOCK_SIZE, 256);
            gOamData[prevSlot + i].split.x = MOD_AND(x + xPosition - BLOCK_SIZE, 512);

            if (doubleSize)
            {
                // Rotation scaling and double size
                gOamData[prevSlot + i].split.affineMode = 3;
            }
            else
            {
                // Rotation scaling
                gOamData[prevSlot + i].split.affineMode = 1;
            }

            // Select proper matrix: mosaic flag doesn't enable mosaic and instead chooses between one of the two matrix slots
            if (mosaic)
            {
                if (gOamData[prevSlot + i].split.xFlip)
                {
                    gOamData[prevSlot + i].split.x--;
                    gOamData[prevSlot + i].split.yFlip = 29 >> 4;
                    gOamData[prevSlot + i].split.xFlip = 29 >> 3;
                    gOamData[prevSlot + i].split.matrixNum = 29;
                }
                else
                {
                    gOamData[prevSlot + i].split.yFlip = 28 >> 4;
                    gOamData[prevSlot + i].split.xFlip = 28 >> 3;
                    gOamData[prevSlot + i].split.matrixNum = 28;
                }
            }
            else
            {
                if (gOamData[prevSlot + i].split.xFlip)
                {
                    gOamData[prevSlot + i].split.x--;
                    gOamData[prevSlot + i].split.yFlip = 31 >> 4;
                    gOamData[prevSlot + i].split.xFlip = 31 >> 3;
                    gOamData[prevSlot + i].split.matrixNum = 31;
                }
                else
                {
                    gOamData[prevSlot + i].split.yFlip = 30 >> 4;
                    gOamData[prevSlot + i].split.xFlip = 30 >> 3;
                    gOamData[prevSlot + i].split.matrixNum = 30;
                }
            }

            if (alphaBlending)
            {
                // Semi transparent
                gOamData[prevSlot + i].split.objMode = OAM_OBJ_MODE_SEMI_TRANSPARENT;
            }

            dst++;
        }
        
        // Update next oam slot
        gNextOamSlot = partCount + prevSlot;

        // Setup matrices for normal and x flip
        
        // [ cos / scaling, -sin / scaling ]
        // [ sin / scaling,  cos / scaling ]
        // and
        // [ cos / -scaling, -sin / scaling ]
        // [ sin / -scaling,  cos / scaling ]
        dy = FixedMultiplication(-SIN(rotation), FixedInverse(scaling));
        dmy = FixedMultiplication(COS(rotation), FixedInverse(scaling));

        if (mosaic)
        {
            gOamData[28 * 4 + 0].all.affineParam = FixedMultiplication(COS(rotation), FixedInverse(scaling));
            gOamData[28 * 4 + 1].all.affineParam = FixedMultiplication(SIN(rotation), FixedInverse(scaling));
            gOamData[28 * 4 + 2].all.affineParam = dy;
            gOamData[28 * 4 + 3].all.affineParam = dmy;

            gOamData[29 * 4 + 0].all.affineParam = FixedMultiplication(COS(rotation), FixedInverse(-scaling));
            gOamData[29 * 4 + 1].all.affineParam = FixedMultiplication(SIN(rotation), FixedInverse(-scaling));
            gOamData[29 * 4 + 2].all.affineParam = dy;
            gOamData[29 * 4 + 3].all.affineParam = dmy;
        }
        else
        {
            gOamData[30 * 4 + 0].all.affineParam = FixedMultiplication(COS(rotation), FixedInverse(scaling));
            gOamData[30 * 4 + 1].all.affineParam = FixedMultiplication(SIN(rotation), FixedInverse(scaling));
            gOamData[30 * 4 + 2].all.affineParam = dy;
            gOamData[30 * 4 + 3].all.affineParam = dmy;

            gOamData[31 * 4 + 0].all.affineParam = FixedMultiplication(COS(rotation), FixedInverse(-scaling));
            gOamData[31 * 4 + 1].all.affineParam = FixedMultiplication(SIN(rotation), FixedInverse(-scaling));
            gOamData[31 * 4 + 2].all.affineParam = dy;
            gOamData[31 * 4 + 3].all.affineParam = dmy;
        }
    }

#ifdef PLATFORM_SWITCH
    /* Keep message banners and their save cursor centered in the wide view. */
    {
        if (!(pSprite->properties & SP_ABSOLUTE_POSITION)) {
            extern void SwitchWorld_NoteSlots(int first,int end,int origin);
            SwitchWorld_NoteSlots(prevSlot,gNextOamSlot,
                (s16)(SUB_PIXEL_TO_PIXEL_(pSprite->xPosition)-SUB_PIXEL_TO_PIXEL(gBg1XPosition)));
        }
        u32 id=pSprite->spriteId;
        bool secondary=(pSprite->properties&SP_SECONDARY_SPRITE)!=0;
        if ((!secondary && (id==PSPRITE_MESSAGE_BANNER || id==PSPRITE_AREA_BANNER)) ||
            (secondary && id==SSPRITE_SAVE_YES_NO_CURSOR)) {
            extern void Port_OverlayText_NoteBannerOam(int first,int end);
            Port_OverlayText_NoteBannerOam(prevSlot,gNextOamSlot);
        }
    }
#endif

#ifdef __3DS__
    /* Tag the OAM span just emitted [prevSlot, gNextOamSlot) as overlay
     * text so the stereo renderer lifts it to the front depth tier instead
     * of leaving it coplanar with Samus. Identity-based (see
     * port_overlay_text_oam.c): the in-game message banner, the area-name
     * banner, and the save YES/NO cursor that rides with the banner. */
    {
        u32 spriteId = pSprite->spriteId;
        u32 secondary = pSprite->properties & SP_SECONDARY_SPRITE;

        /* OAM only keeps Y in 8 bits and X in 9, so a part's real position is
         * ambiguous once the view reaches past the GBA frame (WIDE). Hand the
         * renderer the sprite's true screen position; each part sits within
         * a short distance of it, which is enough to undo the wrap. */
        if (!(pSprite->properties & SP_ABSOLUTE_POSITION))
        {
            extern void Port_Wide_NoteSlots(int firstSlot, int endSlot, int originY, int originX);
            Port_Wide_NoteSlots(prevSlot, gNextOamSlot,
                (s16)(SUB_PIXEL_TO_PIXEL_(pSprite->yPosition) - SUB_PIXEL_TO_PIXEL(gBg1YPosition)),
                (s16)(SUB_PIXEL_TO_PIXEL_(pSprite->xPosition) - SUB_PIXEL_TO_PIXEL(gBg1XPosition)));
        }
        u32 isOverlayText =
            (!secondary && (spriteId == PSPRITE_MESSAGE_BANNER || spriteId == PSPRITE_AREA_BANNER)) ||
            (secondary && spriteId == SSPRITE_SAVE_YES_NO_CURSOR);
        if (isOverlayText)
        {
            extern void Port_OverlayText_NoteBannerOam(int firstSlot, int endSlot);
            Port_OverlayText_NoteBannerOam(prevSlot, gNextOamSlot);
        }

        /* Per-type stereo depth overrides, authored in
         * platform/3ds/source/port_sprite_depth.inc (X-macro; keyed by
         * sprite id + primary/secondary). Absent file -> just the
         * terminator, so the loop never matches. See port_sprite_depth_oam.c
         * for the codes. */
        {
            static const struct { u16 id; u8 secondary; s8 code; } sDepthOverrides[] = {
#if defined(__has_include)
#  if __has_include("port_sprite_depth.inc")
#    define PORT_SPRITE_DEPTH(id_, sec_, code_) { (u16)(id_), (u8)(sec_), (s8)(code_) },
#    include "port_sprite_depth.inc"
#    undef PORT_SPRITE_DEPTH
#  endif
#endif
                { 0xFFFFu, 0u, (s8)PORT_SPRITE_DEPTH_NONE } /* terminator */
            };
            u8 sec = secondary != 0;
            for (u32 k = 0; k < ARRAY_SIZE(sDepthOverrides); k++)
            {
                if (sDepthOverrides[k].id == spriteId && sDepthOverrides[k].secondary == sec)
                {
                    extern void Port_SpriteDepth_NoteSlots(int firstSlot, int endSlot, int code);
                    Port_SpriteDepth_NoteSlots(prevSlot, gNextOamSlot, sDepthOverrides[k].code);
                    break;
                }
            }
        }
    }
#endif
}

/**
 * @brief ddd4 | 150 | Checks if a sprite is on screen
 * 
 * @param pSprite Sprite data pointer
 */
void SpriteCheckOnScreen(struct SpriteData* pSprite)
{
    u16 bgBaseY;
    u16 bgBaseX;
    u16 bgXRange;
    u16 bgYRange;

    u16 spriteY;
    u16 spriteX;
    u16 spriteYRange;
    u16 spriteXRange;
    u16 spriteTop;
    u16 spriteBottom;
    u16 spriteLeft;
    u16 spriteRight;

    u32 drawOffset;

    // Don't bother checking if the sprite has an absolute position
    if (pSprite->properties & SP_ABSOLUTE_POSITION)
        return;

    bgBaseY = gBg1YPosition;
    bgBaseX = gBg1XPosition;

    spriteY = pSprite->yPosition;
    spriteX = pSprite->xPosition;

    bgYRange = bgBaseY + BLOCK_TO_SUB_PIXEL(CEIL(SCREEN_SIZE_X_BLOCKS / 2));
    spriteYRange = spriteY + BLOCK_TO_SUB_PIXEL(CEIL(SCREEN_SIZE_X_BLOCKS / 2));
    spriteBottom = bgYRange - PIXEL_TO_SUB_PIXEL(pSprite->drawDistanceBottom);
    drawOffset = PIXEL_TO_SUB_PIXEL(pSprite->drawDistanceTop) + SCREEN_SIZE_Y_SUB_PIXEL;
    spriteTop = bgYRange + drawOffset;

    bgXRange = bgBaseX + BLOCK_TO_SUB_PIXEL(CEIL(SCREEN_SIZE_X_BLOCKS / 2));
    spriteXRange = spriteX + BLOCK_TO_SUB_PIXEL(CEIL(SCREEN_SIZE_X_BLOCKS / 2));
    spriteLeft = bgXRange - PIXEL_TO_SUB_PIXEL(pSprite->drawDistanceHorizontal);
    drawOffset = PIXEL_TO_SUB_PIXEL(pSprite->drawDistanceHorizontal) + SCREEN_SIZE_X_SUB_PIXEL;
    spriteRight = bgXRange + drawOffset;

#if defined(MZM_3DS)
    {
        // WIDE display: the view reaches past the GBA frame, so sprites there
        // count as on screen. Signed math, the u16 range above would wrap.
        extern int Port_WideMarginSubPixelX(void);
        extern int Port_WideMarginSubPixelY(void);
        s32 marginX = Port_WideMarginSubPixelX();
        s32 marginY = Port_WideMarginSubPixelY();

        if (marginX != 0 || marginY != 0)
        {
            s32 left = (s32)bgXRange - (s32)PIXEL_TO_SUB_PIXEL(pSprite->drawDistanceHorizontal) - marginX;
            s32 right = (s32)bgXRange + (s32)drawOffset + marginX;
            s32 bottom = (s32)bgYRange - (s32)PIXEL_TO_SUB_PIXEL(pSprite->drawDistanceBottom) - marginY;
            s32 top = (s32)bgYRange + (s32)(PIXEL_TO_SUB_PIXEL(pSprite->drawDistanceTop) + SCREEN_SIZE_Y_SUB_PIXEL) + marginY;

            if (left < (s32)spriteXRange && (s32)spriteXRange < right && bottom < (s32)spriteYRange && (s32)spriteYRange < top)
            {
                pSprite->status |= SPRITE_STATUS_ONSCREEN;
                return;
            }
        }
    }
#endif

    if (spriteLeft < spriteXRange && spriteXRange < spriteRight && spriteBottom < spriteYRange && spriteYRange < spriteTop)
    {
        pSprite->status |= SPRITE_STATUS_ONSCREEN;
        return;
    }

    pSprite->status &= ~SPRITE_STATUS_ONSCREEN;

    if (pSprite->properties & SP_KILL_OFF_SCREEN)
    {
        // todo: screen size
        bgYRange = bgBaseY + BLOCK_SIZE * 10;
        spriteYRange = spriteY + BLOCK_SIZE * 10;
        spriteBottom = bgYRange - BLOCK_SIZE * 9;
        spriteTop = bgYRange + BLOCK_SIZE * 19;

        bgXRange = bgBaseX + BLOCK_SIZE * 10;
        spriteXRange = spriteX + BLOCK_SIZE * 10;
        spriteLeft = bgXRange - BLOCK_SIZE * 9;
        spriteRight = bgXRange + BLOCK_SIZE * 24;

        if (spriteLeft >= spriteXRange || spriteXRange >= spriteRight || spriteBottom >= spriteYRange || spriteYRange >= spriteTop)
        {
            // Off range, kill sprite
            pSprite->status = 0;
        }
    }
}

/**
 * @brief df24 | 4c | Calls : SpriteClearData, SpriteLoadSpriteset,
 * EscapeCheckReloadGraphics, SpriteUtilInitLocationText, SpriteLoadRoomSprites
 * and SpawnWaitingPirates
 * 
 */
void SpriteLoadAllData(void)
{
    if (gPauseScreenFlag != PAUSE_SCREEN_NONE)
    {
        // Don't load sprites if psf was active, this indirectly means that the room was already loaded before
        return;
    }

    // Check reset alarm timer
    if (gSubGameMode3 == 0 && !gIsLoadingFile)
        gAlarmTimer = 0;

    // Clear
    SpriteClearData();

    // Load spriteset
    SpriteLoadSpriteset();

    // Setup special sprites
    EscapeCheckReloadGraphics();
    SpriteUtilInitLocationText();

    // Load current room sprites
    SpriteLoadRoomSprites();
    SpawnWaitingPirates();

    gParasiteRelated = 0;
}

/**
 * @brief df84 | 100 | Loads a spriteset
 * 
 */
void SpriteLoadSpriteset(void)
{
    s32 i;
    s32 j;
    s32 spriteset;
    u32 spriteId;
    u32 gfxSlot;
    u32 prevGfxSlot;
    u16 nbrRows;

    s32 ctrl_1;
    s32 ctrl_2;

    for (i = 0; i < MAX_AMOUNT_OF_SPRITE_TYPES; i++)
    {
        gSpritesetSpritesId[i] = PSPRITE_UNUSED16;
        gSpritesetGfxSlots[i] = 0;
    }

    prevGfxSlot = UCHAR_MAX;
    spriteset = gSpriteset;
    if (spriteset >= MAX_AMOUNT_OF_SPRITESET - 1)
    {
        if (gCurrentArea > AREA_TEST)
            spriteset = MAX_AMOUNT_OF_SPRITESET - 1;
        else
            spriteset = 0;
    }

    for (j = 0, i = 0; i < MAX_AMOUNT_OF_SPRITE_TYPES; i++)
    {
        spriteId = sSpritesetPointers[spriteset][j * 2 + 0];
        gfxSlot = sSpritesetPointers[spriteset][j * 2 + 1];

        j++;
        
        if (spriteId == PSPRITE_UNUSED0)
        {
            break;
            EMPTY_DO_WHILE // Needed to produce matching ASM.
        }

        gSpritesetSpritesId[i] = spriteId;
        gSpritesetGfxSlots[i] = MOD_AND(gfxSlot, 8);

        if (gfxSlot == prevGfxSlot)
            continue;

        prevGfxSlot = gfxSlot;
        if (gfxSlot == 8)
            continue;

        spriteId = PSPRITE_OFFSET_FOR_GRAPHICS(spriteId);

        LZ77UncompVram(sSpritesGraphicsPointers[spriteId], VRAM_BASE + 0x14000 + gfxSlot * 2048);

        ctrl_1 = ((u8*)sSpritesGraphicsPointers[spriteId])[1];
        ctrl_2 = ((u8*)sSpritesGraphicsPointers[spriteId])[2] << 8;
        DMA3_COPY_16(sSpritesPalettePointers[spriteId], PALRAM_OBJ + 8 * PAL_ROW_SIZE + gfxSlot * 32, (ctrl_1 | ctrl_2) / 2048 << 4);
    }
}

/**
 * e084 | 2c | Loads the graphics in VRAM for a new sprite
 * 
 * @param spriteId Sprite ID
 * @param row Spriteset Graphics Row
 */
void SpriteLoadGfx(u8 spriteId, u8 row)
{
    spriteId = PSPRITE_OFFSET_FOR_GRAPHICS(spriteId);

    LZ77UncompVram(sSpritesGraphicsPointers[spriteId], VRAM_BASE + 0x14000 + (row * 0x800));
}

/**
 * e0b0 | 40 | Loads the palette in PALRAM for a new sprite
 * 
 * @param spriteId Sprite ID
 * @param row Palette Row
 * @param len Length (in rows)
 */
void SpriteLoadPal(u8 spriteId, u8 row, u8 len)
{
    spriteId = PSPRITE_OFFSET_FOR_GRAPHICS(spriteId);

    DMA3_COPY_16(sSpritesPalettePointers[spriteId], PALRAM_OBJ + 8 * PAL_ROW_SIZE + (row * 16 * sizeof(u16)), len * 16);
}

/**
 * @brief e0f0 | 44 | Clears the sprite data (including debris)
 * 
 */
void SpriteClearData(void)
{
    s32 i;

    // Clear sprites
    for (i = 0; i < MAX_AMOUNT_OF_SPRITES; i++)
    {
        gSpriteData[i].status = 0;
        gSpriteData[i].standingOnSprite = SAMUS_STANDING_ON_SPRITE_OFF;
        gSpriteData[i].roomSlot = UCHAR_MAX;
    }

    // Clear debris
    for (i = 0; i < MAX_AMOUNT_OF_SPRITE_DEBRIS; i++)
        gSpriteDebris[i].exists = FALSE;
}

/**
 * @brief e134 | 48 | Loads the sprites of the current room
 * 
 */
void SpriteLoadRoomSprites(void)
{
    u8 i;
    u8 y;
    u8 x;
    u8 slot;
    
    for (i = 0; i < MAX_AMOUNT_OF_SPRITES; i++)
    {
        /*
            0 | Y position
            1 | X position
            2 | Spriteset slot
        */
        y = gCurrentRoomEntry.pEnemyRoomData[i * ENEMY_ROOM_DATA_SIZE + 0];

        // Terminator
        if (y == UCHAR_MAX)
            break;

        x = gCurrentRoomEntry.pEnemyRoomData[i * ENEMY_ROOM_DATA_SIZE + 1];
        slot = gCurrentRoomEntry.pEnemyRoomData[i * ENEMY_ROOM_DATA_SIZE + 2];
        SpriteInitPrimary(slot, y, x, i);
    }
}

/**
 * @brief e17c | dc | Initializes a primary sprite with the values in parameters
 * 
 * @param spritesetSlot Spriteset slot/properties
 * @param yPosition Y Position (blocks)
 * @param xPosition X Position (blocks)
 * @param roomSlot Room Slot
 */
void SpriteInitPrimary(u8 spritesetSlot, u16 yPosition, u16 xPosition, u8 roomSlot)
{
    u8 ramSlot;
    struct SpriteData* pSprite;

    // Try to find an empty slot
    for (ramSlot = 0, pSprite = gSpriteData; pSprite < gSpriteData + MAX_AMOUNT_OF_SPRITES; ramSlot++, pSprite++)
    {
        if (pSprite->status & SPRITE_STATUS_EXISTS)
            continue;

        // Found empty slot, mark exists
        pSprite->status = SPRITE_STATUS_EXISTS;

        // Get 7 first bits of the spriteset slot
        spritesetSlot = MOD_AND(spritesetSlot, 128);

        // A spriteset slot value that's above MAX_AMOUNT_OF_SPRITE_TYPES + 1 means the spriteset should be used
        if (spritesetSlot > MAX_AMOUNT_OF_SPRITE_TYPES + 1)
        {
            spritesetSlot--;

            // Modulo to not overflow the next array access
            spritesetSlot = MOD_AND(spritesetSlot, MAX_AMOUNT_OF_SPRITE_TYPES + 1);

            // Fetch the gfx slot and the sprite id
            pSprite->spritesetGfxSlot = gSpritesetGfxSlots[spritesetSlot];
            pSprite->spriteId = gSpritesetSpritesId[spritesetSlot];
        }
        else
        {
            // Don't use the spriteset, directly write to sprite id and use first gfx slot
            pSprite->spritesetGfxSlot = 0;
            pSprite->spriteId = spritesetSlot - 1;
        }

        pSprite->properties = 0;

        // Convert block position to sub pixel such that the new position is in the bottom middle of the block in sub pixels
        pSprite->yPosition = BLOCK_TO_SUB_PIXEL(yPosition) + BLOCK_SIZE;
        pSprite->xPosition = BLOCK_TO_SUB_PIXEL(xPosition) + HALF_BLOCK_SIZE;
        pSprite->roomSlot = roomSlot;

        pSprite->bgPriority = 2;
        pSprite->drawOrder = 4;
        pSprite->pose = SPRITE_POSE_UNINITIALIZED;
        pSprite->health = 0;
        pSprite->invincibilityStunFlashTimer = 0;

        pSprite->paletteRow = 0;
        pSprite->frozenPaletteRowOffset = 0;
        pSprite->absolutePaletteRow = 0;

        pSprite->ignoreSamusCollisionTimer = DELTA_TIME;
        pSprite->primarySpriteRamSlot = ramSlot;
        pSprite->freezeTimer = 0;
        pSprite->standingOnSprite = SAMUS_STANDING_ON_SPRITE_OFF;
        break;
    }
}

/**
 * e258 | c4 | Spawns a new secondary sprite with the given parameters
 * 
 * @param spriteId The ID of the sprite to spawn
 * @param partNumber Part number
 * @param gfxSlot The sprite graphics slot (usually the same as the primary sprite)
 * @param ramSlot The RAM slot of the secondary sprite's parent
 * @param yPosition Y Position
 * @param xPosition X Position
 * @param statusToAdd Additional status flags (default are Exists, On Screen and Not Drawn)
 * @return The assigned RAM slot of the spawned sprite, 0xFF is the sprite couldn't spawn
 */
u8 SpriteSpawnSecondary(u8 spriteId, u8 partNumber, u8 gfxSlot, u8 ramSlot, u16 yPosition, u16 xPosition, u16 statusToAdd)
{
    u8 newSlot;
    struct SpriteData* pSprite;
#ifdef MZM_3DS
#endif

    // Try to find an empty slot
    for (newSlot = 0, pSprite = gSpriteData; pSprite < gSpriteData + MAX_AMOUNT_OF_SPRITES; newSlot++, pSprite++)
    {
        if (pSprite->status & SPRITE_STATUS_EXISTS)
            continue;

        pSprite->status = statusToAdd | (SPRITE_STATUS_EXISTS | SPRITE_STATUS_ONSCREEN | SPRITE_STATUS_NOT_DRAWN);
        pSprite->properties = SP_SECONDARY_SPRITE;

        pSprite->spritesetGfxSlot = gfxSlot;
        pSprite->spriteId = spriteId;
        pSprite->yPosition = yPosition;
        pSprite->xPosition = xPosition;
        pSprite->roomSlot = partNumber;

        pSprite->bgPriority = 2;
        pSprite->drawOrder = 4;

        pSprite->pose = SPRITE_POSE_UNINITIALIZED;
        pSprite->health = 0;
        pSprite->invincibilityStunFlashTimer = 0;

        pSprite->paletteRow = 0;
        pSprite->frozenPaletteRowOffset = 0;
        pSprite->absolutePaletteRow = 0;

        pSprite->ignoreSamusCollisionTimer = 1 * DELTA_TIME;

        pSprite->primarySpriteRamSlot = ramSlot;

        pSprite->freezeTimer = 0;
        pSprite->standingOnSprite = SAMUS_STANDING_ON_SPRITE_OFF;

        return newSlot;
    }

    return UCHAR_MAX;
}

/**
 * e31c | b8 | Spawns a new primary sprite with the given parameters
 * 
 * @param spriteId The ID of the sprite to spawn
 * @param partNumber Part number
 * @param gfxSlot The sprite graphics slot
 * @param yPosition Y Position
 * @param xPosition X Position
 * @param statusToAdd Additional status flags (default are Exists, On Screen and Not Drawn)
 * @return The assigned RAM slot of the spawned sprite, 0xFF if the sprite couldn't spawn
 */
u8 SpriteSpawnPrimary(u8 spriteId, u8 partNumber, u8 gfxSlot, u16 yPosition, u16 xPosition, u16 statusToAdd)
{
    u8 newSlot;
    struct SpriteData* pSprite;

    // Try to find an empty slot
    for (newSlot = 0, pSprite = gSpriteData; pSprite < gSpriteData + MAX_AMOUNT_OF_SPRITES; newSlot++, pSprite++)
    {
        if (pSprite->status & SPRITE_STATUS_EXISTS)
            continue;

        pSprite->status = statusToAdd | (SPRITE_STATUS_EXISTS | SPRITE_STATUS_ONSCREEN | SPRITE_STATUS_NOT_DRAWN);
        pSprite->properties = 0;

        pSprite->spritesetGfxSlot = gfxSlot;
        pSprite->spriteId = spriteId;
        pSprite->yPosition = yPosition;
        pSprite->xPosition = xPosition;
        pSprite->roomSlot = partNumber;

        pSprite->bgPriority = 2;
        pSprite->drawOrder = 4;

        pSprite->pose = SPRITE_POSE_UNINITIALIZED;
        pSprite->health = 0;
        pSprite->invincibilityStunFlashTimer = 0;

        pSprite->paletteRow = 0;
        pSprite->frozenPaletteRowOffset = 0;
        pSprite->absolutePaletteRow = 0;

        pSprite->ignoreSamusCollisionTimer = DELTA_TIME;

        pSprite->primarySpriteRamSlot = newSlot;

        pSprite->freezeTimer = 0;
        pSprite->standingOnSprite = SAMUS_STANDING_ON_SPRITE_OFF;

        return newSlot;
    }

    return UCHAR_MAX;
}

/**
 * e3d4 | b8 | Spawns a new primary sprite with the given parameters (used only for the drops and the followers sprite)
 * 
 * @param spriteId The ID of the sprite to spawn
 * @param partNumber The room slot
 * @param gfxSlot The sprite graphics slot
 * @param ramSlot The RAM slot of the sprite's parent
 * @param yPosition Y Position
 * @param xPosition X Position
 * @param statusToAdd Additional status flags (default are Exists, On Screen and Not Drawn)
 * @return The assigned RAM slot of the spawned sprite, 0xFF is the sprite couldn't spawn
 */
u8 SpriteSpawnDropFollowers(u8 spriteId, u8 partNumber, u8 gfxSlot, u8 ramSlot, u16 yPosition, u16 xPosition, u16 statusToAdd)
{
    u8 newSlot;
    struct SpriteData* pSprite;

    // Try to find an empty slot
    for (newSlot = 0, pSprite = gSpriteData; pSprite < gSpriteData + MAX_AMOUNT_OF_SPRITES; newSlot++, pSprite++)
    {
        if (pSprite->status & SPRITE_STATUS_EXISTS)
            continue;

        pSprite->status = statusToAdd | (SPRITE_STATUS_EXISTS | SPRITE_STATUS_ONSCREEN | SPRITE_STATUS_NOT_DRAWN);
        pSprite->properties = 0;

        pSprite->spritesetGfxSlot = gfxSlot;
        pSprite->spriteId = spriteId;
        pSprite->yPosition = yPosition;
        pSprite->xPosition = xPosition;
        pSprite->roomSlot = partNumber;

        pSprite->bgPriority = 2;
        pSprite->drawOrder = 4;

        pSprite->pose = SPRITE_POSE_UNINITIALIZED;
        pSprite->health = 0;
        pSprite->invincibilityStunFlashTimer = 0;

        pSprite->paletteRow = 0;
        pSprite->frozenPaletteRowOffset = 0;
        pSprite->absolutePaletteRow = 0;

        pSprite->ignoreSamusCollisionTimer = DELTA_TIME;

        pSprite->primarySpriteRamSlot = ramSlot;

        pSprite->freezeTimer = 0;
        pSprite->standingOnSprite = SAMUS_STANDING_ON_SPRITE_OFF;

        return newSlot;
    }

    return UCHAR_MAX;
}
