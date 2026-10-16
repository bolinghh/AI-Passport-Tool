#include "play.h"
#include <string.h>

static uint32_t random_next(pc_play_t *p) {
    uint32_t x=p->random;
    x^=x<<13;x^=x>>17;x^=x<<5;
    p->random=x;return x;
}
static void changed(pc_play_t *p,uint32_t now) {
    if(++p->revision==0) ++p->revision;
    p->changed_at=now;
}
static void reward(pc_play_t *p,unsigned xp,unsigned treats,uint32_t now) {
    uint32_t amount=p->progress.xp+xp;
    p->progress.xp=amount>99999?99999:amount;
    unsigned food=p->progress.treats+treats;
    p->progress.treats=(uint16_t)(food>999?999:food);
    changed(p,now);
}
void pc_play_init(pc_play_t *p,uint32_t seed) {
    memset(p,0,sizeof(*p));p->random=seed?seed:0x73c5a9b1;p->progress.treats=3;
}
unsigned pc_pet_level(const pc_progress_t *p) {return p->xp>=900?10:1+p->xp/100;}
unsigned pc_pet_outfits(const pc_progress_t *p) {
    unsigned level=pc_pet_level(p);return level>=4?3:level>=2?2:1;
}
unsigned pc_fish_discovered(const pc_progress_t *p) {
    unsigned count=0;for(unsigned i=0;i<PC_FISH_COUNT;i++) count+=p->fish[i]>0;return count;
}
const char *pc_fish_name(unsigned species) {
    static const char *const names[]={"小银鱼","金鱼","蓝鳍鱼","河豚","锦鲤","月光鱼"};
    return species<PC_FISH_COUNT?names[species]:"未知鱼种";
}
const char *pc_fish_rarity(unsigned species) {
    return species<2?"常见":species<4?"少见":species==4?"稀有":"传说";
}
void pc_play_tick(pc_play_t *p,uint32_t now) {
    p->now=now;
    if(p->reaction!=PC_PET_QUIET && now-p->reaction_at>=3000) p->reaction=PC_PET_QUIET;
    if(p->fishing==PC_FISH_WAIT || p->fishing==PC_FISH_BITE) {
        uint32_t elapsed=now-p->cast_at;
        if(elapsed>=p->wait_ms+PC_FISH_BITE_MS) {p->fishing=PC_FISH_MISSED;p->early=false;}
        else if(elapsed>=p->wait_ms) p->fishing=PC_FISH_BITE;
    }
}
void pc_play_focus_completed(pc_play_t *p,uint32_t now) {reward(p,20,2,now);}
void pc_pet_key(pc_play_t *p,unsigned key,uint32_t now) {
    pc_play_tick(p,now);
    if(key<2) {p->pet_action=(p->pet_action+(key==0?2:1))%3;return;}
    if(key!=2) return;
    p->reaction_at=now;
    if(p->pet_action==0) {
        if(!p->progress.treats) {p->reaction=PC_PET_NO_FOOD;return;}
        --p->progress.treats;reward(p,5,0,now);p->reaction=PC_PET_FED;
    } else if(p->pet_action==1) p->reaction=PC_PET_HAPPY;
    else {
        unsigned next=(p->progress.outfit+1)%pc_pet_outfits(&p->progress);
        if(next!=p->progress.outfit) {p->progress.outfit=(uint8_t)next;changed(p,now);}
        p->reaction=PC_PET_DRESSED;
    }
}
void pc_fishing_cancel(pc_play_t *p) {p->fishing=PC_FISH_IDLE;}
void pc_fishing_key(pc_play_t *p,unsigned key,uint32_t now) {
    bool active=p->fishing==PC_FISH_WAIT || p->fishing==PC_FISH_BITE;
    pc_play_tick(p,now);
    if(active && p->fishing==PC_FISH_MISSED) return;
    if(key>2) return;
    if(p->fishing==PC_FISH_BOOK) {
        if(key==2) p->fishing=PC_FISH_IDLE;
        else p->book=(p->book+(key==0?PC_FISH_COUNT-1:1))%PC_FISH_COUNT;
    } else if(p->fishing==PC_FISH_WAIT) {
        if(key==2) {p->fishing=PC_FISH_MISSED;p->early=true;}
    } else if(p->fishing==PC_FISH_BITE) {
        if(key!=2) return;
        p->fishing=PC_FISH_CAUGHT;
        if(p->progress.fish[p->species]<999) ++p->progress.fish[p->species];
        reward(p,8,1,now);
    } else if(key<2) p->fishing=PC_FISH_BOOK;
    else {
        /* Roll once per cast. Inputs cannot repeatedly reroll a pending bite. */
        unsigned roll=random_next(p)%100;
        const unsigned cumulative[]={40,65,82,92,98,100};
        for(unsigned i=0;i<PC_FISH_COUNT;i++) if(roll<cumulative[i]) {p->species=(uint8_t)i;break;}
        p->cast_at=now;p->wait_ms=2000+random_next(p)%3001;p->early=false;p->fishing=PC_FISH_WAIT;
    }
}
static void put16(uint8_t *p,uint16_t v) {p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static uint16_t get16(const uint8_t *p) {return (uint16_t)((p[0]<<8)|p[1]);}
static void put32(uint8_t *p,uint32_t v) {for(int i=3;i>=0;i--) {p[i]=(uint8_t)v;v>>=8;}}
static uint32_t get32(const uint8_t *p) {return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint32_t crc32(const uint8_t *p,size_t size) {
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<size;i++) {
        crc^=p[i];for(unsigned bit=0;bit<8;bit++) crc=(crc>>1)^(0xedb88320U&-(crc&1));
    }
    return ~crc;
}
void pc_progress_encode(const pc_progress_t *p,uint8_t out[PC_PLAY_SAVE_SIZE]) {
    memset(out,0,PC_PLAY_SAVE_SIZE);memcpy(out,"PXP1",4);
    put32(out+4,p->xp);put16(out+8,p->treats);out[10]=p->outfit;
    for(unsigned i=0;i<PC_FISH_COUNT;i++) put16(out+16+i*2,p->fish[i]);
    put32(out+28,crc32(out,28));
}
bool pc_progress_decode(pc_progress_t *p,const uint8_t *data,size_t size) {
    if(!data || size!=PC_PLAY_SAVE_SIZE || memcmp(data,"PXP1",4) || get32(data+28)!=crc32(data,28)) return false;
    for(unsigned i=11;i<16;i++) if(data[i]) return false;
    pc_progress_t value={.xp=get32(data+4),.treats=get16(data+8),.outfit=data[10]};
    if(value.xp>99999 || value.treats>999 || value.outfit>=pc_pet_outfits(&value)) return false;
    for(unsigned i=0;i<PC_FISH_COUNT;i++) {
        value.fish[i]=get16(data+16+i*2);if(value.fish[i]>999) return false;
    }
    *p=value;return true;
}
