/* Host preview and renderer checks, from the repository root:
   clang -std=c99 -fsanitize=address,undefined -g -Iapp_xghostbusters/src
     tests/validate_display.c app_xghostbusters/src/game.c -o /tmp/validate_display
   /tmp/validate_display [preview.ppm]
   The optional image shows intro, gameplay and game-over at native resolution. */
#include <assert.h>
#include <stdlib.h>
#define DISPLAY_HOST_PREVIEW
#include "../app_xghostbusters/src/display.c"

static uint16_t preview[320][720];

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
    game_t g;game_init(&g,1);check_frame(&g,0);
    g.frame=150;g.score=2400;g.pitch_hz=220;g.voice=1;g.cooldown=9;
    for(unsigned i=0;i<3;++i) {
        g.ghosts[i]=(game_ghost_t){180-(int)i*20,i,1,i};
        g.shots[i]=(game_shot_t){92+(int)i*8,i,1};
    }
    g.effects[0]=(game_effect_t){210,1,5,1};check_frame(&g,1);
    for(unsigned frame=0;frame<48;++frame) {
        g.frame=150+frame;g.lane=frame%3;g.voice=frame%2;
        g.hum=!g.voice;g.invincible=frame%5;g.hit_flash=frame%8;
        for(unsigned i=0;i<GAME_GHOSTS;++i)
            g.ghosts[i]=(game_ghost_t){-18+(int)((frame*13+i*37)%280),i%3,1,i%3};
        for(unsigned i=0;i<GAME_SHOTS;++i)
            g.shots[i]=(game_shot_t){-10+(int)((frame*17+i*53)%280),i%3,1};
        for(unsigned i=0;i<GAME_EFFECTS;++i)
            g.effects[i]=(game_effect_t){(int)(i*47),i%3,1+frame%14,i%3};
        check_frame(&g,-1);
    }
    /* Real gameplay must create a capture at the collision, then expire it. */
    game_init(&g,1);g.frame=150;
    g.ghosts[0]=(game_ghost_t){70,1,1,2};game_step(&g,0,1);
    assert(g.effects[0].age==1 && g.effects[0].kind==2 && g.score==400);
    for(unsigned i=0;i<14;++i)game_step(&g,0,0);
    assert(!g.effects[0].age);
    g.over=1;g.lives=0;g.best=2400;g.restart_ready=1;check_frame(&g,2);
    g.restart_ready=0;
    for(unsigned quiet=0;quiet<=30;++quiet){g.quiet=quiet;check_frame(&g,-1);}
    g.score=UINT32_MAX;g.best=UINT32_MAX;check_frame(&g,-1);
    if(argc>1) {
        FILE *f=fopen(argv[1],"wb");if(!f){perror(argv[1]);return 1;}
        fprintf(f,"P6\n720 320\n255\n");
        for(unsigned y=0;y<320;++y)for(unsigned x=0;x<720;++x) {
            unsigned c=preview[y][x];
            unsigned char rgb[3]={((c>>11)&31)*255/31,((c>>5)&63)*255/63,(c&31)*255/31};
            if(fwrite(rgb,1,3,f)!=3){fclose(f);return 1;}
        }
        if(fclose(f))return 1;
    }
    puts("PASS: deterministic strips, clipped actors/effects, capture lifetime and preview states");
    return 0;
}
