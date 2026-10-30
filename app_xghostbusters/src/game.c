#include <string.h>
#include "game.h"

static unsigned random_next(game_t *g) {
    unsigned x=g->random;x^=x<<13;x^=x>>17;x^=x<<5;
    return g->random=x;
}
void game_init(game_t *g,unsigned seed) {
    memset(g,0,sizeof(*g));g->lives=3;g->lane=1;
    g->candidate=1;g->spawn=GAME_FPS*3;g->random=seed?seed:0x47b057;
    g->title=1;g->boss_next=1000;g->slimer_next=300;
}
static void award(game_t *g,unsigned points) {
    g->score+=points;if(g->score>g->best)g->best=g->score;
}
static void damage(game_t *g) {
    if(g->invincible || g->over)return;
    --g->lives;g->hit_flash=8;g->invincible=60;g->slime_ticks=GAME_SLIME_FRAMES;
    if(!g->lives){g->over=1;g->quiet=0;g->restart_ready=0;}
}
static void capture_ghost(game_t *g,int x,unsigned lane,unsigned kind) {
    award(g,kind==3?500:100u<<kind);
    for(unsigned i=0;i<GAME_EFFECTS;++i)if(!g->effects[i].age) {
        g->effects[i]=(game_effect_t){x,lane,1,kind};break;
    }
}
static void boss_step(game_t *g) {
    ++g->boss_frame;
    if(g->boss_flash)--g->boss_flash;
    if(g->boss_defeat) {
        if(++g->boss_defeat>GAME_BOSS_DEFEAT) {
            g->boss_active=0;g->boss_defeat=0;g->spawn=90;
            g->boss_next=g->score+2500;
            g->location_flash=60;
        }
        return;
    }
    if(g->boss_frame<GAME_BOSS_ENTRANCE)return;
    /* A full second of lane warning precedes each travelling marshmallow. */
    if(!g->boss_attack) {
        g->boss_lane=random_next(g)%3;g->boss_attack=90;
    }
    --g->boss_attack;
    if(g->boss_attack==60)for(unsigned i=0;i<GAME_HAZARDS;++i)
        if(!g->hazards[i].active) {
            g->hazards[i]=(game_hazard_t){164,g->boss_lane,1};break;
        }
    for(unsigned i=0;i<GAME_HAZARDS;++i) {
        game_hazard_t *h=&g->hazards[i];if(!h->active)continue;
        h->x-=5;
        if(h->lane==g->lane && h->x<56 && h->x>13 && !g->invincible) {
            h->active=0;damage(g);
        }
        if(h->x<-12)h->active=0;
    }
}
void game_step(game_t *g,unsigned pitch_hz,unsigned voice) {
    ++g->frame;g->voice=voice!=0;g->pitch_hz=pitch_hz;
    if(g->title) {
        /* Ignore boot-time microphone settling, then accept speech to start. */
        if(g->frame>=GAME_FPS && voice) {
            g->title=0;g->frame=0;g->cooldown=15;
        }
        return;
    }
    for(unsigned i=0;i<GAME_EFFECTS;++i)
        if(g->effects[i].age && ++g->effects[i].age>GAME_CAPTURE_FRAMES)g->effects[i].age=0;
    if(g->over) {
        if(!voice) {if(g->quiet<30)++g->quiet;if(g->quiet==30)g->restart_ready=1;}
        else {
            g->quiet=0;
            if(g->restart_ready) {
                unsigned best=g->best,seed=g->random;
                game_init(g,seed);g->best=best;g->title=0;g->cooldown=15;
            }
        }
        return;
    }
    /* Hysteresis around 170/260 Hz and three stable display frames (~100 ms). */
    unsigned lane=g->lane;
    if(pitch_hz>=80 && pitch_hz<=500) {
        if(g->lane==2)lane=pitch_hz>275?0:(pitch_hz>185?1:2);
        else if(g->lane==1)lane=pitch_hz<155?2:(pitch_hz>275?0:1);
        else lane=pitch_hz<155?2:(pitch_hz<245?1:0);
        if(lane!=g->candidate){g->candidate=lane;g->candidate_frames=1;}
        else if(g->candidate_frames<3)++g->candidate_frames;
        if(g->candidate_frames>=3)g->lane=lane;
    } else g->candidate_frames=0;
    if(g->invincible)--g->invincible;
    if(g->hit_flash)--g->hit_flash;
    if(g->slime_ticks)--g->slime_ticks;
    if(g->location_flash)--g->location_flash;
    unsigned location=(g->score/750)%GAME_LOCATIONS;
    if(location!=g->location){g->location=location;g->location_flash=60;}
    if(g->cooldown)--g->cooldown;
    if(!g->boss_active && g->score>=g->boss_next) {
        g->boss_active=1;g->boss_frame=0;g->boss_hp=GAME_BOSS_HP;
        g->boss_flash=0;g->boss_attack=0;
        memset(g->ghosts,0,sizeof(g->ghosts));
        memset(g->shots,0,sizeof(g->shots));
        g->slimer.active=0;g->slimer_next=g->score+500;
    }
    if(g->boss_active)boss_step(g);
    if(g->over)return;
    if(g->boss_defeat)return;
    if(!g->boss_active && !g->slimer.active && g->score>=g->slimer_next) {
        unsigned lane=random_next(g)%3;
        g->slimer=(game_slimer_t){254,lane,1,0,(lane+1)%3,2,0};
    }
    if(voice && !g->cooldown) {
        for(unsigned i=0;i<GAME_SHOTS;++i)if(!g->shots[i].active) {
            g->shots[i]=(game_shot_t){51,g->lane,1};g->cooldown=10;
            ++g->shots_fired;break;
        }
    }
    unsigned difficulty=g->score/500;
    if(difficulty>6)difficulty=6;
    if(!g->boss_active && g->spawn)--g->spawn;
    if(!g->boss_active && !g->spawn) {
        for(unsigned i=0;i<GAME_GHOSTS;++i)if(!g->ghosts[i].active) {
            unsigned r=random_next(g);
            unsigned roll=random_next(g)%100;
            unsigned kind=roll<65?0:(roll<90?1:2);
            g->ghosts[i]=(game_ghost_t){245,r%3,1,kind};break;
        }
        g->spawn=55-difficulty*5+(random_next(g)%16);
    }
    for(unsigned i=0;i<GAME_SHOTS;++i) {
        game_shot_t *s=&g->shots[i];if(!s->active)continue;
        int previous=s->x;s->x+=10;
        if(g->boss_active && g->boss_frame>=GAME_BOSS_ENTRANCE && s->x+12>=164) {
            s->active=0;g->boss_flash=5;
            if(!--g->boss_hp) {
                g->boss_defeat=1;++g->bosses_defeated;award(g,1500);
                memset(g->hazards,0,sizeof(g->hazards));
                memset(g->shots,0,sizeof(g->shots));
                break;
            }
        }
        if(!s->active)continue;
        if(g->slimer.active && g->slimer.lane==s->lane &&
           s->x+12>=g->slimer.x-16 && previous<=g->slimer.x+18) {
            s->active=0;g->slimer.flash=6;
            if(!--g->slimer.hp) {
                g->slimer.active=0;
                capture_ghost(g,g->slimer.x,g->slimer.lane,3);
                g->slimer_next=g->score+750;
            }
        }
        if(!s->active)continue;
        for(unsigned j=0;j<GAME_GHOSTS;++j) {
            game_ghost_t *ghost=&g->ghosts[j];
            if(ghost->active && ghost->lane==s->lane &&
               s->x+12>=ghost->x-12 && previous<=ghost->x+16) {
                ghost->active=0;s->active=0;
                capture_ghost(g,ghost->x,ghost->lane,ghost->kind);
                break;
            }
        }
        if(s->x>250)s->active=0;
    }
    for(unsigned i=0;i<GAME_GHOSTS;++i) {
        game_ghost_t *ghost=&g->ghosts[i];if(!ghost->active)continue;
        /* Green 1x, cyan 1.5x, purple 2x; alternate half pixels for
           exact long-term ratios at every difficulty, without floating point. */
        unsigned twice_speed=(2+difficulty/2)*(2+ghost->kind);
        ghost->x-=(int)(twice_speed/2+((twice_speed&1)&&(g->frame&1)));
        if(ghost->lane==g->lane && ghost->x<56 && ghost->x>13 && !g->invincible) {
            ghost->active=0;damage(g);
            if(g->over)break;
        }
        if(ghost->x<-18)ghost->active=0;
    }
    if(g->over)return;
    if(g->slimer.active) {
        game_slimer_t *s=&g->slimer;
        ++s->age;s->x-=2;if(s->flash)--s->flash;
        /* Telegraph lane hops for ten frames, and never hop near the player. */
        if(s->age%45==0 && s->x>80) {
            s->lane=s->next_lane;s->next_lane=(s->lane+1)%3;
        }
        if(s->lane==g->lane && s->x<60 && s->x>10 && !g->invincible) {
            s->active=0;damage(g);g->slimer_next=g->score+750;
        }
        if(s->x<-24){s->active=0;g->slimer_next=g->score+750;}
    }
}
