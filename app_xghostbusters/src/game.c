#include <string.h>
#include "game.h"

static unsigned random_next(game_t *g) {
    unsigned x=g->random;x^=x<<13;x^=x>>17;x^=x<<5;
    return g->random=x;
}
void game_init(game_t *g,unsigned seed) {
    memset(g,0,sizeof(*g));g->lives=3;g->lane=1;
    g->candidate=1;g->spawn=GAME_FPS*3;g->random=seed?seed:0x47b057;
}
void game_step(game_t *g,unsigned pitch_hz,unsigned voice) {
    ++g->frame;g->voice=voice!=0;g->pitch_hz=pitch_hz;
    for(unsigned i=0;i<GAME_EFFECTS;++i)
        if(g->effects[i].age && ++g->effects[i].age>14)g->effects[i].age=0;
    if(g->over) {
        if(!voice) {if(g->quiet<30)++g->quiet;if(g->quiet==30)g->restart_ready=1;}
        else {
            g->quiet=0;
            if(g->restart_ready) {
                unsigned best=g->best,seed=g->random;
                game_init(g,seed);g->best=best;
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
    if(g->cooldown)--g->cooldown;
    if(voice && !g->cooldown) {
        for(unsigned i=0;i<GAME_SHOTS;++i)if(!g->shots[i].active) {
            g->shots[i]=(game_shot_t){51,g->lane,1};g->cooldown=10;
            ++g->shots_fired;break;
        }
    }
    unsigned difficulty=g->score/500;
    if(difficulty>6)difficulty=6;
    if(g->spawn)--g->spawn;
    if(!g->spawn) {
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
        for(unsigned j=0;j<GAME_GHOSTS;++j) {
            game_ghost_t *ghost=&g->ghosts[j];
            if(ghost->active && ghost->lane==s->lane &&
               s->x+12>=ghost->x-12 && previous<=ghost->x+16) {
                ghost->active=0;s->active=0;g->score+=100u<<ghost->kind;
                for(unsigned k=0;k<GAME_EFFECTS;++k)if(!g->effects[k].age) {
                    g->effects[k]=(game_effect_t){ghost->x,ghost->lane,1,ghost->kind};break;
                }
                if(g->score>g->best)g->best=g->score;
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
            ghost->active=0;--g->lives;g->hit_flash=8;g->invincible=60;
            if(!g->lives){g->over=1;g->quiet=0;g->restart_ready=0;break;}
        }
        if(ghost->x<-18)ghost->active=0;
    }
}
