/* From the repository root: clang -std=c99 -Iapp_xghostbusters/src
   tests/validate_game.c app_xghostbusters/src/{game,pitch,controls}.c
   -lm -o /tmp/validate_game && /tmp/validate_game */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "pitch.h"
#include "controls.h"

static void play_init(game_t *g,unsigned seed) {
    game_init(g,seed);g->title=0;g->boss_next=UINT32_MAX;
}

static void check_pitch(void) {
    int32_t pcm[1024];
    const unsigned frequencies[]={80,90,120,160,170,220,260,330,440,490,500};
    for(unsigned f=0;f<sizeof(frequencies)/sizeof(frequencies[0]);++f) {
        unsigned hz=frequencies[f];
        for(unsigned harmonic=0;harmonic<2;++harmonic) {
            for(unsigned i=0;i<1024;++i) {
                double phase=2*3.141592653589793*hz*i/16000;
                double v=sin(phase)*0.08;
                if(harmonic)v+=sin(phase*2)*0.15+sin(phase*3)*0.04;
                pcm[i]=(int32_t)((v+0.03)*2147483647);
            }
            unsigned detected=pitch_detect(pcm);
            printf("pitch %u Hz, harmonics %u -> %u Hz\n",hz,harmonic,detected);
            assert(abs((int)detected-(int)hz)<=4);
        }
    }
    memset(pcm,0,sizeof(pcm));assert(!pitch_detect(pcm));
    unsigned random=42;
    for(unsigned i=0;i<1024;++i) {
        random=random*1664525u+1013904223u;
        pcm[i]=(int32_t)random/4;
    }
    assert(!pitch_detect(pcm));
    for(unsigned i=0;i<1024;++i)pcm[i]=123456789;
    assert(!pitch_detect(pcm));
    /* Board microphone tones can be only -69 dBFS: retain quiet tonal hums. */
    for(unsigned i=0;i<1024;++i)
        pcm[i]=(int32_t)(0.0005*2147483647*sin(2*3.141592653589793*120*i/16000));
    assert(pitch_detect(pcm)==120);
}
static void check_game(void) {
    game_t g;play_init(&g,1);
    assert(g.lives==3 && g.lane==1 && !g.over);
    for(unsigned i=0;i<3;++i)game_step(&g,120,0);
    assert(g.lane==2);
    for(unsigned i=0;i<10;++i)game_step(&g,175,0);
    assert(g.lane==2); /* Hysteresis. */
    for(unsigned i=0;i<3;++i)game_step(&g,220,0);
    assert(g.lane==1);
    for(unsigned i=0;i<3;++i)game_step(&g,330,0);
    assert(g.lane==0);
    for(unsigned i=0;i<3;++i)game_step(&g,0,0);
    assert(g.lane==0);
    assert(!g.shots[0].active);
    game_step(&g,0,1);assert(g.shots[0].active && g.cooldown==10);
    g.ghosts[0]=(game_ghost_t){85,0,1,0};
    for(unsigned i=0;i<4;++i)game_step(&g,0,0);
    assert(g.score==100 && g.best==100 && !g.ghosts[0].active);
    memset(g.shots,0,sizeof(g.shots));
    for(unsigned life=3;life;--life) {
        g.invincible=0;g.ghosts[0]=(game_ghost_t){52,0,1,0};
        game_step(&g,0,0);assert(g.lives==life-1);
        if(life>1) {
            g.ghosts[0]=(game_ghost_t){52,0,1,0};game_step(&g,0,0);
            assert(g.lives==life-1); /* Protected from multiple hits. */
        }
    }
    assert(g.over);
    game_step(&g,0,1);assert(g.over); /* No held-voice instant restart. */
    for(unsigned i=0;i<30;++i)game_step(&g,0,0);
    assert(g.restart_ready);game_step(&g,0,1);
    assert(!g.over && g.lives==3 && !g.score && g.best==100);
    /* Long run through spawning, movement, firing, collisions and restarts. */
    for(unsigned i=0;i<30000;++i) {
        game_step(&g,(i/90)%3==0?120:((i/90)%3==1?220:330),(i%120)<80);
        assert(g.lane<3 && g.lives<=3 && g.over==!g.lives);
        for(unsigned j=0;j<GAME_GHOSTS;++j)assert(g.ghosts[j].lane<3);
    }
}
static void check_controls(void) {
    controls_t c={0};
    /* Noisy hum onset followed by steady/sliding hum, even with full VAD. */
    for(unsigned i=0;i<3;++i){controls_step(&c,0,1,1000,1);assert(!c.fire);}
    for(unsigned i=0;i<120;++i) {
        controls_step(&c,120+i,1,1000,1);assert(c.hum && !c.fire);
        if(i%10==0){controls_step(&c,0,1,1000,1);assert(!c.fire);}
    }
    /* Silence with a lingering high VAD must not fire. */
    for(unsigned i=0;i<12;++i){controls_step(&c,0,1,900,0);assert(!c.fire);}
    /* Low current score with held VAD cannot fire either. */
    for(unsigned i=0;i<12;++i){controls_step(&c,0,1,100,1);assert(!c.fire);}
    for(unsigned i=0;i<5;++i)controls_step(&c,0,1,950,1);
    assert(c.fire && !c.hum);
    controls_step(&c,220,1,950,1);assert(c.hum && !c.fire);
    /* Hum-to-speech transition works without VAD first becoming quiet. */
    for(unsigned i=0;i<6;++i)controls_step(&c,0,1,950,1);
    assert(c.fire);
    controls_step(&c,0,0,0,0);assert(!c.fire);
}
static void check_ghost_types(void) {
    const unsigned points[]={100,200,400};
    for(unsigned kind=0;kind<3;++kind) {
        game_t g;play_init(&g,1);
        g.ghosts[0]=(game_ghost_t){200,0,1,kind};
        for(unsigned i=0;i<10;++i)game_step(&g,0,0);
        assert(g.ghosts[0].x==200-(int)(10*(2+kind)));
        g.ghosts[0]=(game_ghost_t){70,1,1,kind};
        game_step(&g,0,1);assert(g.score==points[kind] && g.best==points[kind]);
        /* Also verify 1.5x fractional steps at an odd base speed. */
        g.score=1000;g.ghosts[0]=(game_ghost_t){200,0,1,kind};
        for(unsigned i=0;i<10;++i)game_step(&g,0,0);
        assert(g.ghosts[0].x==200-(int)(15*(2+kind)));
    }
    game_t g;play_init(&g,42);unsigned count[3]={0};
    for(unsigned i=0;i<20000;++i) {
        memset(g.ghosts,0,sizeof(g.ghosts));g.spawn=0;
        game_step(&g,0,0);assert(g.ghosts[0].active);
        assert(g.ghosts[0].kind<3);++count[g.ghosts[0].kind];
    }
    printf("Spawn sample: green %u cyan %u purple %u\n",count[0],count[1],count[2]);
    assert(count[0]>12600 && count[0]<13400);
    assert(count[1]>4600 && count[1]<5400);
    assert(count[2]>1600 && count[2]<2400);
}
static void check_title_and_boss(void) {
    game_t g;game_init(&g,42);
    for(unsigned i=0;i<29;++i)game_step(&g,120,i<10);
    assert(g.title && g.frame==29 && g.spawn==90 && !g.shots_fired);
    game_step(&g,0,1);assert(!g.title && !g.frame && g.cooldown==15);
    for(unsigned i=0;i<14;++i)game_step(&g,0,0);
    assert(!g.shots_fired);
    g.score=1000;g.ghosts[0]=(game_ghost_t){70,1,1,0};
    game_step(&g,0,0);
    assert(g.boss_active && g.boss_hp==GAME_BOSS_HP && !g.ghosts[0].active);
    /* Shots cannot hurt the boss during his entrance. */
    g.shots[0]=(game_shot_t){160,1,1};game_step(&g,0,0);
    assert(g.boss_hp==GAME_BOSS_HP);
    while(g.boss_frame<GAME_BOSS_ENTRANCE-1)game_step(&g,0,0);
    memset(g.shots,0,sizeof(g.shots));
    game_step(&g,0,0);assert(g.boss_attack==89);
    for(unsigned i=0;i<29;++i)game_step(&g,0,0);
    assert(g.boss_attack==60 && g.hazards[0].active);
    g.lane=g.hazards[0].lane;
    for(unsigned i=0;i<23;++i)game_step(&g,0,0);
    assert(g.lives==2 && g.invincible && !g.over);
    g.hazards[0]=(game_hazard_t){57,g.lane,1};
    game_step(&g,0,0);assert(g.lives==2); /* Invulnerability also covers boss attacks. */
    g.invincible=0;g.hazards[0]=(game_hazard_t){57,(g.lane+1)%3,1};
    game_step(&g,0,0);assert(g.lives==2); /* A different lane safely dodges it. */
    memset(g.hazards,0,sizeof(g.hazards));
    for(unsigned hp=GAME_BOSS_HP;hp;--hp) {
        g.shots[0]=(game_shot_t){155,hp%3,1};game_step(&g,0,0);
        assert(g.boss_hp==hp-1);
    }
    assert(g.boss_defeat==1 && g.score==2500 && g.best==2500 && g.bosses_defeated==1);
    for(unsigned i=0;i<GAME_BOSS_DEFEAT;++i)game_step(&g,0,1);
    assert(!g.boss_active && g.boss_next==5000);
    assert(g.lives==2); /* Defeat animation is safe, even with held speech. */
    game_step(&g,0,0);assert(!g.boss_active);
    g.score=5000;game_step(&g,0,0);
    assert(g.boss_active && g.boss_hp==GAME_BOSS_HP);
    /* Restart discards the boss, traps and hazards, preserving only the best. */
    g.over=1;g.lives=0;g.restart_ready=1;
    game_step(&g,0,1);
    assert(!g.title && !g.over && !g.boss_active && g.lives==3 && g.best==2500);
    assert(!g.hazards[0].active && !g.effects[0].age && g.boss_next==1000);
    puts("PASS: speech-to-start, boss entrance, warned attacks, damage, victory and restart");
}

int main(void) {
    check_pitch();check_controls();check_game();check_ghost_types();
    check_title_and_boss();
    puts("PASS: hum/speech filter, pitch, ghost speeds/rarity/rewards, lives and restart");
}
