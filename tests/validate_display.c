/* Host preview and renderer checks, from the repository root:
   clang -std=c99 -fsanitize=address,undefined -g -Iapp_xghostbusters/src
     tests/validate_display.c app_xghostbusters/src/game.c -o /tmp/validate_display
   /tmp/validate_display [preview.ppm]
     The optional image includes title, gameplay, boss, traps and haunted locations. */
#include <assert.h>
#include <stdlib.h>
#define DISPLAY_HOST_PREVIEW
#include "../app_xghostbusters/src/display.c"

#define PREVIEW_WIDTH 2400
static uint16_t preview[320][PREVIEW_WIDTH];

static void check_frame(const game_t *g,int panel) {
    /* Different initial contents must produce identical, fully painted strips. */
    for(strip_y=0;strip_y<320;strip_y+=8) {
        memset(strip[0],0xa5,sizeof(strip[0]));
        memset(strip[1],0x5a,sizeof(strip[1]));
        canvas=strip[0];draw_scene(g);
        canvas=strip[1];draw_scene(g);
        assert(!memcmp(strip[0],strip[1],sizeof(strip[0])));
        if(panel>=0)for(int row=0;row<8;++row)
            memcpy(&preview[strip_y+row][panel*240],&canvas[row*240],240*2);
    }
}

int main(int argc,char **argv) {
    int left,right;
    memset(strip,0,sizeof(strip));
    assert(!changed_columns(strip[0],strip[1],&left,&right));
    strip[1][3*240+110]=1;strip[1][7*240+125]=1;
    assert(changed_columns(strip[0],strip[1],&left,&right) && left==110 && right==126);
    strip[1][0]=1;strip[1][8*240-1]=1;
    assert(changed_columns(strip[0],strip[1],&left,&right) && !left && right==240);
    game_t g;game_init(&g,1);check_frame(&g,0);
    g.title=0;g.frame=150;g.score=2400;g.pitch_hz=220;g.voice=1;g.cooldown=9;
    for(unsigned i=0;i<3;++i) {
        g.ghosts[i]=(game_ghost_t){180-(int)i*20,i,1,i};
        g.shots[i]=(game_shot_t){92+(int)i*8,i,1};
    }
    g.effects[0]=(game_effect_t){210,1,5,1};check_frame(&g,1);
    game_t traps=g;
    memset(traps.ghosts,0,sizeof(traps.ghosts));
    for(unsigned i=0;i<3;++i)
        traps.effects[i]=(game_effect_t){140+(int)i*20,i,4+i*11,i};
    check_frame(&traps,4);
    game_t scenery=traps;
    memset(scenery.effects,0,sizeof(scenery.effects));
    scenery.slimer=(game_slimer_t){169,1,1,38,2,2,0};
    for(unsigned location=0;location<GAME_LOCATIONS;++location) {
        scenery.location=location;scenery.location_flash=20;
        check_frame(&scenery,5+(int)location);
    }
    for(unsigned frame=0;frame<48;++frame) {
        g.frame=150+frame;g.lane=frame%3;g.voice=frame%2;
        g.hum=!g.voice;g.invincible=frame%5;g.hit_flash=frame%8;
        for(unsigned i=0;i<GAME_GHOSTS;++i)
            g.ghosts[i]=(game_ghost_t){-18+(int)((frame*13+i*37)%280),i%3,1,i%3};
        for(unsigned i=0;i<GAME_SHOTS;++i)
            g.shots[i]=(game_shot_t){-10+(int)((frame*17+i*53)%280),i%3,1};
        for(unsigned i=0;i<GAME_EFFECTS;++i)
            g.effects[i]=(game_effect_t){(int)(i*47),i%3,1+frame%GAME_CAPTURE_FRAMES,i%4};
        g.location=frame%GAME_LOCATIONS;g.slime_ticks=frame%GAME_SLIME_FRAMES;
        g.slimer=(game_slimer_t){-24+(int)(frame*7%300),frame%3,1,frame,
                                  (frame+1)%3,1,frame%6};
        check_frame(&g,-1);
    }
    /* Real gameplay must create a capture at the collision, then expire it. */
    game_init(&g,1);g.title=0;g.frame=150;
    g.ghosts[0]=(game_ghost_t){70,1,1,2};game_step(&g,0,1);
    assert(g.effects[0].age==1 && g.effects[0].kind==2 && g.score==400);
    for(unsigned i=0;i<GAME_CAPTURE_FRAMES;++i)game_step(&g,0,0);
    assert(!g.effects[0].age);
    g.over=1;g.lives=0;g.best=2400;g.restart_ready=1;check_frame(&g,2);
    g.restart_ready=0;
    for(unsigned quiet=0;quiet<=30;++quiet){g.quiet=quiet;check_frame(&g,-1);}
    g.score=UINT32_MAX;g.best=UINT32_MAX;check_frame(&g,-1);
    game_init(&g,1);g.title=0;g.frame=500;g.score=1000;
    g.boss_active=1;g.boss_hp=7;g.boss_frame=90;g.boss_lane=2;g.boss_attack=80;
    g.shots[0]=(game_shot_t){130,1,1};g.lane=1;g.voice=1;g.cooldown=9;
    check_frame(&g,3);
    for(unsigned phase=0;phase<150;++phase) {
        g.boss_frame=phase;g.boss_flash=phase%6;g.boss_hp=phase%(GAME_BOSS_HP+1);
        g.boss_defeat=phase>60?phase-60:0;
        g.hazards[0]=(game_hazard_t){-12+(int)phase*2,phase%3,1};
        check_frame(&g,-1);
    }
    game_init(&g,1);
    for(unsigned frame=0;frame<48;++frame){g.frame=frame;check_frame(&g,-1);}
    game_init(&g,1);g.title=0;g.frame=150;g.slime_ticks=GAME_SLIME_FRAMES;
    for(unsigned age=0;age<GAME_SLIME_FRAMES;++age) {
        g.slime_ticks=GAME_SLIME_FRAMES-age;g.location=age%GAME_LOCATIONS;
        check_frame(&g,-1);
    }
    g.location=1;g.slime_ticks=36;g.slimer=(game_slimer_t){170,1,1,18,2,2,0};
    check_frame(&g,9);
    if(argc>1) {
        FILE *f=fopen(argv[1],"wb");if(!f){perror(argv[1]);return 1;}
        fprintf(f,"P6\n%d 320\n255\n",PREVIEW_WIDTH);
        for(unsigned y=0;y<320;++y)for(unsigned x=0;x<PREVIEW_WIDTH;++x) {
            unsigned c=preview[y][x];
            unsigned char rgb[3]={((c>>11)&31)*255/31,((c>>5)&63)*255/63,(c&31)*255/31};
            if(fwrite(rgb,1,3,f)!=3){fclose(f);return 1;}
        }
        if(fclose(f))return 1;
    }
    puts("PASS: deterministic strips, clipped actors/effects, capture lifetime and preview states");
    return 0;
}
