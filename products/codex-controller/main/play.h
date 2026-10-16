#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PC_FISH_COUNT 6U
#define PC_PLAY_SAVE_SIZE 32U
#define PC_FISH_BITE_MS 1800U

typedef enum { PC_FISH_IDLE, PC_FISH_WAIT, PC_FISH_BITE, PC_FISH_CAUGHT,
               PC_FISH_MISSED, PC_FISH_BOOK } pc_fish_state_t;
typedef enum { PC_PET_QUIET, PC_PET_FED, PC_PET_HAPPY, PC_PET_NO_FOOD, PC_PET_DRESSED } pc_pet_reaction_t;
typedef struct {
    uint32_t xp;
    uint16_t treats, fish[PC_FISH_COUNT];
    uint8_t outfit;
} pc_progress_t;
typedef struct {
    pc_progress_t progress;
    uint32_t random, now, cast_at, wait_ms, reaction_at;
    /* revision changes only when durable progress changes. Zero means clean boot. */
    uint32_t revision, changed_at;
    pc_fish_state_t fishing;
    pc_pet_reaction_t reaction;
    uint8_t pet_action, species, book;
    bool early, save_failed, saving;
} pc_play_t;

void pc_play_init(pc_play_t *, uint32_t seed);
void pc_play_tick(pc_play_t *, uint32_t now);
void pc_play_focus_completed(pc_play_t *, uint32_t now);
void pc_pet_key(pc_play_t *, unsigned key, uint32_t now);
void pc_fishing_key(pc_play_t *, unsigned key, uint32_t now);
void pc_fishing_cancel(pc_play_t *);
unsigned pc_pet_level(const pc_progress_t *);
unsigned pc_pet_outfits(const pc_progress_t *);
unsigned pc_fish_discovered(const pc_progress_t *);
const char *pc_fish_name(unsigned species);
const char *pc_fish_rarity(unsigned species);
/* Versioned, endian-independent blob, CRC32; invalid data never partially loads. */
void pc_progress_encode(const pc_progress_t *, uint8_t out[PC_PLAY_SAVE_SIZE]);
bool pc_progress_decode(pc_progress_t *, const uint8_t *, size_t);
