#pragma once
#include "play.h"
typedef void (*pc_pixel_fn)(void *,int,int,int,int,uint32_t);
typedef struct {void *context;pc_pixel_fn pixel;} pc_canvas_t;
void pc_art_pet(pc_canvas_t,int x,int y,unsigned scale,unsigned outfit,pc_pet_reaction_t,unsigned frame);
void pc_art_fish(pc_canvas_t,int x,int y,unsigned scale,unsigned species,bool known);
void pc_art_pet_room(pc_canvas_t,const pc_play_t *);
void pc_art_pond(pc_canvas_t,const pc_play_t *);
