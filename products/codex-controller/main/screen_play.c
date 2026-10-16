/* Original pixel art, authored for the Passport 240 x 320 screen. All imagery
 * is spans drawn into the existing LVGL partial buffer, never a new canvas. */
#include "screen_play.h"
static const uint32_t OUTLINE=0x001125, FUR=0x79d9cf, LIGHT=0xc8f5df, SHADE=0x3da4ad;
static void rect(pc_canvas_t c,int x,int y,int w,int h,uint32_t color) {c.pixel(c.context,x,y,w,h,color);}
static void star(pc_canvas_t c,int x,int y,int scale,uint32_t color) {
    rect(c,x+scale,y,scale,3*scale,color);rect(c,x,y+scale,3*scale,scale,color);
}
void pc_art_pet(pc_canvas_t c,int x,int y,unsigned scale,unsigned outfit,pc_pet_reaction_t reaction,unsigned frame) {
    int s=(int)scale,bob=frame%4==1 || frame%4==2?-s:0;y+=bob;
    rect(c,x+2*s,y+16*s,12*s,s,0x25495d);
    /* Tufted ears, rounded stepped body and feet. */
    rect(c,x+2*s,y,3*s,5*s,OUTLINE);rect(c,x+11*s,y,3*s,5*s,OUTLINE);
    rect(c,x+3*s,y+s,s,3*s,FUR);rect(c,x+12*s,y+s,s,3*s,FUR);
    rect(c,x+2*s,y+3*s,12*s,12*s,OUTLINE);rect(c,x+s,y+5*s,14*s,8*s,OUTLINE);
    rect(c,x+3*s,y+4*s,10*s,10*s,FUR);rect(c,x+2*s,y+6*s,12*s,6*s,FUR);
    rect(c,x+4*s,y+4*s,7*s,2*s,LIGHT);rect(c,x+3*s,y+6*s,2*s,3*s,LIGHT);
    rect(c,x+11*s,y+10*s,2*s,3*s,SHADE);rect(c,x+5*s,y+13*s,6*s,s,SHADE);
    rect(c,x+3*s,y+14*s,3*s,2*s,OUTLINE);rect(c,x+10*s,y+14*s,3*s,2*s,OUTLINE);
    rect(c,x+3*s,y+14*s,2*s,s,FUR);rect(c,x+11*s,y+14*s,2*s,s,FUR);
    bool happy=reaction==PC_PET_FED || reaction==PC_PET_HAPPY;
    int eye_height=happy || frame==0?1:2;
    rect(c,x+5*s,y+8*s,s,eye_height*s,OUTLINE);rect(c,x+10*s,y+8*s,s,eye_height*s,OUTLINE);
    rect(c,x+7*s,y+10*s,2*s,s,OUTLINE);
    rect(c,x+3*s,y+10*s,2*s,s,0xffa7a3);rect(c,x+11*s,y+10*s,2*s,s,0xffa7a3);
    if(outfit==1) {
        rect(c,x+3*s,y+12*s,10*s,s,0xffbe60);rect(c,x+10*s,y+13*s,2*s,3*s,0xf58b53);
    } else if(outfit==2) {
        star(c,x+6*s,y-3*s,s,0xffd478);rect(c,x+5*s,y+s,6*s,s,0xffbd55);
    }
    if(happy) {star(c,x-s,y+3*s,s,0xffbd55);star(c,x+15*s,y+s,s,0xffbd55);}
}
void pc_art_fish(pc_canvas_t c,int x,int y,unsigned scale,unsigned species,bool known) {
    static const uint32_t colors[]={0xb8d6e5,0xffc55c,0x75b5f3,0xf4dd9d,0xff8967,0xb5a0ed};
    int s=(int)scale;uint32_t body=known?colors[species%PC_FISH_COUNT]:0x3c5a76;
    rect(c,x+4*s,y,6*s,s,OUTLINE);rect(c,x+2*s,y+s,10*s,6*s,OUTLINE);
    rect(c,x+s,y+3*s,12*s,2*s,OUTLINE);rect(c,x+4*s,y+7*s,6*s,s,OUTLINE);
    rect(c,x+12*s,y+s,2*s,6*s,OUTLINE);rect(c,x+14*s,y,2*s,8*s,OUTLINE);
    rect(c,x+3*s,y+2*s,8*s,4*s,body);rect(c,x+4*s,y+s,6*s,6*s,body);
    rect(c,x+13*s,y+2*s,2*s,4*s,body);rect(c,x+15*s,y+s,s,6*s,body);
    if(known) {
        rect(c,x+5*s,y+s,3*s,s,0xffedbd);rect(c,x+4*s,y+3*s,s,s,OUTLINE);
        rect(c,x+7*s,y+4*s,3*s,s,0x578fa5);
        if(species==4) {rect(c,x+9*s,y+2*s,2*s,2*s,0xffedbd);rect(c,x+5*s,y+5*s,2*s,s,0xffedbd);}
        if(species==5) {star(c,x-s,y-s,s,0xffd478);star(c,x+17*s,y+4*s,s,0xffd478);}
    }
}
static void line(pc_canvas_t c,int x,int y,int end_x,int end_y,uint32_t color) {
    int dx=end_x>x?end_x-x:x-end_x,dy=end_y>y?y-end_y:end_y-y;
    int sx=x<end_x?1:-1,sy=y<end_y?1:-1,error=dx+dy;
    for(;;) {
        rect(c,x,y,2,2,color);if(x==end_x && y==end_y) break;
        int twice=2*error;if(twice>=dy) {error+=dy;x+=sx;}if(twice<=dx) {error+=dx;y+=sy;}
    }
}
void pc_art_pet_room(pc_canvas_t c,const pc_play_t *p) {
    rect(c,20,98,200,81,0x0d2b43);rect(c,20,173,200,6,0x294957);
    rect(c,29,163,20,10,0xc7825a);rect(c,33,169,12,6,0x8d5d50);
    rect(c,37,143,3,20,0x3da58a);rect(c,29,147,9,5,0x79d9ac);rect(c,40,142,9,5,0x79d9ac);
    rect(c,183,117,22,28,0x24485d);rect(c,186,120,16,22,0x0b2238);
    star(c,189,126,3,0xffbd55);star(c,62,116,2,0x3c5a76);
    pc_art_pet(c,88,106,4,p->progress.outfit,p->reaction,(p->now/450)%16);
    rect(c,45,84,150,3,0x25415a);rect(c,45,84,pc_pet_level(&p->progress)==10?150:(int)(p->progress.xp%100)*150/100,3,0x6fddb1);
}
void pc_art_pond(pc_canvas_t c,const pc_play_t *p) {
    if(p->fishing==PC_FISH_BOOK) {
        rect(c,20,87,200,117,0x0e304a);
        pc_art_fish(c,72,117,5,p->book,p->progress.fish[p->book]>0);
        for(unsigned i=0;i<PC_FISH_COUNT;i++) rect(c,77+(int)i*15,187,7,4,i==p->book?0xffbd55:0x3c5a76);
        return;
    }
    rect(c,16,89,208,116,0x103c56);rect(c,16,89,208,43,0x123248);
    rect(c,16,122,208,7,0x386b69);rect(c,16,129,208,2,0x72b299);
    for(int i=0;i<5;i++) {
        rect(c,19+i*10,111-i%3*4,3,15+i%3*4,0x4f9b7e);
        rect(c,18+i*10,108-i%3*4,5,7,0x70bf91);
    }
    for(int i=0;i<9;i++) {
        int shift=(int)(p->now/400%3)*2;
        rect(c,88+i%3*42+shift,146+i/3*20,14,2,0x286480);
        rect(c,96+i%3*42-shift,151+i/3*20,9,2,0x1b526f);
    }
    rect(c,16,134,72,8,0xc79268);rect(c,16,142,72,4,0x77584f);
    for(int i=0;i<4;i++) rect(c,21+i*18,135,2,5,0x977054);
    rect(c,24,146,6,36,0x77584f);rect(c,72,146,6,24,0x77584f);
    pc_art_pet(c,32,101,2,p->progress.outfit,PC_PET_QUIET,2);
    line(c,61,121,106,95,0xd6bb87);line(c,106,95,136,p->fishing==PC_FISH_BITE?158:151+(int)(p->now/350%3),0x9dbbd5);
    int bob=(int)(p->now/350%3);
    if(p->fishing==PC_FISH_WAIT || p->fishing==PC_FISH_BITE) {
        int y=p->fishing==PC_FISH_BITE?158:150+bob;rect(c,133,y,8,4,0xffedbd);rect(c,135,y+4,4,5,0xff785c);
        if(p->fishing==PC_FISH_BITE) {
            rect(c,156,118,5,16,0xffbd55);rect(c,156,139,5,5,0xffbd55);
            uint32_t elapsed=p->now-p->cast_at-p->wait_ms;
            uint32_t left=elapsed<PC_FISH_BITE_MS?PC_FISH_BITE_MS-elapsed:0;
            rect(c,24,208,192,4,0x25415a);rect(c,24,208,(int)(left*192/PC_FISH_BITE_MS),4,0xffbd55);
        }
    } else if(p->fishing==PC_FISH_CAUGHT) {
        pc_art_fish(c,113,154,4,p->species,true);
    }
}
