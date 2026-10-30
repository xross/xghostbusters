#include <stdint.h>
#include <string.h>
#include <stdio.h>
#ifndef DISPLAY_HOST_PREVIEW
#include <platform.h>
#include <xcore/port.h>
#include <xcore/channel.h>
#include <xcore/hwtimer.h>
#include "spi_fwk.h"
#include "app.h"
#endif
#include "game.h"

#ifndef DISPLAY_HOST_PREVIEW
static spi_master_t bus;
static spi_master_device_t lcd;
static uint8_t pixels[240*2] __attribute__((aligned(8)));
/* Retain the last LCD image for exact dirty comparisons without drawing twice. */
static uint16_t previous_frame[240*320] __attribute__((aligned(8)));
#endif
/* The second scratch strip is also used by host determinism checks. */
static uint16_t strip[2][240*8] __attribute__((aligned(8)));
static uint16_t *canvas;
static int strip_y;
#ifndef DISPLAY_HOST_PREVIEW
volatile game_t game_diag;
volatile unsigned display_frames, display_last_spectrum, display_frame_ticks;
volatile unsigned display_peak_ticks, display_budget_misses, display_reset_stats;
static void wait_ms(unsigned ms) {
    hwtimer_t t=hwtimer_alloc(); hwtimer_delay(t,ms*100000); hwtimer_free(t);
}
static void command(uint8_t cmd, const uint8_t *data,unsigned len) {
    spi_master_start_transaction(&lcd);
    port_out(PORT_LCD_DC,0);
    spi_master_transfer(&lcd,&cmd,NULL,1);
    if(len) {port_out(PORT_LCD_DC,1);spi_master_transfer(&lcd,(uint8_t*)data,NULL,len);}
    spi_master_end_transaction(&lcd);
}
static void area(unsigned x,unsigned y,unsigned w,unsigned h) {
    uint8_t a[4]={x>>8,x,(x+w-1)>>8,x+w-1}; command(0x2a,a,4);
    uint8_t b[4]={y>>8,y,(y+h-1)>>8,y+h-1}; command(0x2b,b,4);
    uint8_t cmd=0x2c;
    spi_master_start_transaction(&lcd);port_out(PORT_LCD_DC,0);
    spi_master_transfer(&lcd,&cmd,NULL,1);port_out(PORT_LCD_DC,1);
}
static void lcd_fill(unsigned x,unsigned y,unsigned w,unsigned h,uint16_t color) {
    for(unsigned i=0;i<w;++i){pixels[2*i]=color>>8;pixels[2*i+1]=color;}
    area(x,y,w,h);
    for(unsigned row=0;row<h;++row)spi_master_transfer(&lcd,pixels,NULL,w*2);
    spi_master_end_transaction(&lcd);
}
#endif
static void fill(int x,int y,int w,int h,uint16_t color) {
    int right=x+w,bottom=y+h;
    if(x<0)x=0;if(right>240)right=240;
    if(y<strip_y)y=strip_y;if(bottom>strip_y+8)bottom=strip_y+8;
    for(int row=y;row<bottom;++row)for(int col=x;col<right;++col)
        canvas[(row-strip_y)*240+col]=color;
}
// Compact 3x5 glyphs for on-screen labels; each row occupies three bits.
static unsigned glyph(char c) {
    switch(c) {
    case '0':return 075557;case '1':return 026227;case '2':return 071747;
    case '3':return 071717;case '4':return 055711;case '5':return 074717;
    case '6':return 074757;case '7':return 071111;case '8':return 075757;
    case '9':return 075717;case 'A':return 025755;case 'C':return 034443;
    case 'B':return 065656;case 'D':return 065556;
    case 'E':return 074647;case 'F':return 074644;case 'G':return 034553;case 'H':return 055755;
    case 'I':return 072227;case 'K':return 055655;case 'L':return 044447;
    case 'M':return 057555;case 'N':return 057755;case 'O':return 025552;
    case 'P':return 075744;case 'Q':return 025573;case 'R':return 075765;case 'S':return 034217;
    case 'T':return 072222;case 'U':return 055557;case 'V':return 055552;
    case 'W':return 055575;case 'Y':return 055222;
    case 'X':return 055255;case 'Z':return 071247;case '-':return 000700;
    case '.':return 000002;case '/':return 011244;default:return 0;
    }
}
static void text(unsigned x,unsigned y,const char *s,unsigned size,uint16_t color) {
    if((int)y>=strip_y+8 || (int)(y+5*size)<=strip_y)return;
    while(*s){unsigned bits=glyph(*s++);
        for(unsigned row=0;row<5;++row)for(unsigned col=0;col<3;++col)
            if(bits&(1u<<((4-row)*3+2-col)))fill(x+col*size,y+row*size,size,size,color);
        x+=4*size;
    }
}
enum {BG=0x0823, GREEN=0x87ed, WHITE=0xefbf, RED=0xf986,
      GOLD=0xfe60, CYAN=0x4eff, DARK=0x10a5, SUIT=0xc4ed,
      INK=0x0022, PANEL=0x10a6, MUTED=0x7bd1, PURPLE=0xc3ff};
static int lane_y(unsigned lane){return 94+68*(int)lane;}
static const uint16_t ghost_colors[3]={GREEN,CYAN,PURPLE};
static const uint16_t ghost_shadows[3]={0x33c8,0x1b97,0x6194};

static void sprite(int x,int y,const char *const rows[],unsigned height,
                   uint16_t body,uint16_t shadow,unsigned bob) {
    if(y+(int)(height*2+bob)<=strip_y || y+(int)bob>=strip_y+8)return;
    int top=y+(int)bob;
    unsigned first=strip_y>top?(unsigned)(strip_y-top)/2:0;
    unsigned end=(unsigned)(strip_y+8-top+1)/2;if(end>height)end=height;
    for(unsigned row=first;row<end;++row)for(unsigned col=0;rows[row][col];++col) {
        char c=rows[row][col];if(c==' ')continue;
        uint16_t color;
        switch(c) {
        case '#':color=body;break;case 's':color=shadow;break;
        case 'W':color=WHITE;break;case 'R':color=RED;break;
        case 'G':color=GOLD;break;case 'C':color=CYAN;break;
        case 'F':color=0xf5d0;break;case 'B':color=0x7b6a;break;
        default:color=INK;break;
        }
        fill(x+(int)col*2,y+(int)row*2+(int)bob,2,2,color);
    }
}
static const char *const ghost_sprite[]={
    "    ####    ","  ##WW###   "," ##W#####s  "," #WW#####ss ",
    " ##DD##DDss "," ##WD##WDss "," ########ss ","  ###DD#sss ",
    " s###RD#sss ","s#########ss","s#########ss"," s##s##s##s ",
    "  #ss #ss #s","   s   s   s"};
static const char *const wisp_sprite[]={
    "      ##    ","    ##W##   ","   #WW###s  ","  ##W####ss ",
    "  #DD##DDss ","  #WD##WDss "," ########sss","ss###DD##ss ",
    " s########s ","  s######s  ","   s####s   ","    s###s   ",
    "     s##    ","      s##   "};
static const char *const phantom_sprite[]={
    " ##      ## "," #W######ss "," ##WW####ss "," ##W#####ss ",
    " #DD####DDs "," ##WD##WDss "," ########ss ","s###DDDD##ss",
    "s###WRRW##ss"," s#######ss "," ss#####ss  ","  s#ss#ss#s ",
    "  ss  ss ss ","  s    s  s "};
static const char *const buster_sprite[]={
    "    DDDD    ","   DFFFFD   ","   DFCCCF   ","    FFFF    ",
    " DDDs##s#   ","DGRDs##R##  ","DDCDs##s#FFD","DGRDs##sDDDD",
    " DDDsBBBs   ","  Ds####s   ","   s#ss#s   ","   s#  #s   ",
    "   s#  #s   ","  DDD  DDD  "};

static void draw_ghost(int x,int y,unsigned kind,unsigned frame,unsigned index) {
    unsigned phase=(frame/6+index)%4;
    unsigned bob=phase==1?0:(phase==3?2:1);
    /* Offset wisps and a pale leading edge suggest a glow without blending. */
    fill(x-3,y+12+(int)bob,2,5,ghost_shadows[kind]);
    fill(x+25,y+8+(int)bob,2,3,ghost_shadows[kind]);
    sprite(x,y,kind==1?wisp_sprite:(kind==2?phantom_sprite:ghost_sprite),14,
           ghost_colors[kind],ghost_shadows[kind],bob);
    if(phase&1) {
        fill(x+4,y+26+(int)bob,2,2,ghost_colors[kind]);
        fill(x+16,y+28+(int)bob,2,2,ghost_shadows[kind]);
    }
}

static void bolt(int x,int y,unsigned frame) {
    if(y-8>=strip_y+8 || y+9<=strip_y)return;
    static const int wave[8]={0,-2,-3,0,3,1,-2,0};
    for(int i=0;i<12;++i) {
        int offset=wave[(i+frame/2)%8];
        int px=x-8+i*2;
        fill(px,y+offset-3,3,7,0x7927);
        fill(px,y+offset-1,3,3,GOLD);
        fill(px,y+offset,2,1,WHITE);
        fill(px,y-offset+4,3,2,0x2375);
        fill(px,y-offset+4,2,1,CYAN);
    }
    fill(x+14,y-2,4,5,WHITE);
    fill(x+19,y-5,2,2,GOLD);fill(x+18,y+6,2,2,CYAN);
}

static void capture(const game_effect_t *e) {
    int x=e->x,y=lane_y(e->lane),age=(int)e->age;
    if(y-30>=strip_y+8 || y+28<=strip_y)return;
    if(x<18)x=18;if(x>222)x=222;
    uint16_t color=ghost_colors[e->kind];
    if(age<24) {
        /* Striped light cone narrows toward the open trap. */
        for(int row=0;row<32;row+=2) {
            int half=16-row/3;
            fill(x-half,y-12+row,half*2,1,0x222b);
            fill(x-half,y-12+row,1,1,GOLD);
            fill(x+half,y-12+row,1,1,CYAN);
        }
        const char *const *rows=e->kind==1?wisp_sprite:
                               (e->kind==2?phantom_sprite:ghost_sprite);
        static const int spiral[8]={0,3,4,2,0,-2,-4,-2};
        int ghost_x=x+(age>6?spiral[((age-7)/2)%8]*(24-age)/17:0);
        int width=age<7?24:24-(age-7),height=age<7?28:28-(age-7);
        int top=y-14+(age>6?(age-6)*2:0);
        for(int row=0;row<14;++row)for(int col=0;col<12;++col) {
            char c=rows[row][col];if(c==' ')continue;
            int left=ghost_x-width/2+col*width/12,next=ghost_x-width/2+(col+1)*width/12;
            int yy=top+row*height/14,bottom=top+(row+1)*height/14;
            if(yy<y+20)fill(left,yy,next-left,bottom-yy,
                c=='D'?INK:((age<6 && age%2)?WHITE:color));
        }
    }
    /* Yellow/black hazard stripes, split lid and a lit containment indicator. */
    fill(x-12,y+19,24,7,INK);fill(x-10,y+20,20,4,0x6b4d);
    for(int i=0;i<4;++i)fill(x-10+i*5,y+20,3,2,GOLD);
    if(age<24) {
        fill(x-15,y+15,8,3,0x9cf3);fill(x+7,y+15,8,3,0x9cf3);
        fill(x-7,y+19,14,2,WHITE);
    } else {
        fill(x-12,y+17,24,3,0x9cf3);fill(x+7,y+22,2,2,GREEN);
        int r=(age-24)*2+3;
        fill(x-r,y+15-r,2,2,CYAN);fill(x+r,y+15-r,2,2,GOLD);
        fill(x-r/2,y+10-r,2,2,WHITE);fill(x+r/2,y+8-r,2,2,color);
    }
    char points[8];snprintf(points,sizeof(points),"%u",100u<<e->kind);
    int tx=x-6;if(tx<2)tx=2;if(tx>226)tx=226;
    text(tx,y-20-e->age/4,points,1,age<24?WHITE:color);
}

static void centered(int y,const char *label,unsigned size,uint16_t color) {
    int width=(int)strlen(label)*4*(int)size-(int)size;
    text((240-width)/2,y,label,size,color);
}

static void disc(int cx,int cy,int radius,uint16_t color) {
    /* Scan only the current strip: the large illustration needs no bitmap RAM. */
    int top=cy-radius,bottom=cy+radius;
    if(top<strip_y)top=strip_y;if(bottom>=strip_y+8)bottom=strip_y+7;
    for(int y=top;y<=bottom;++y) {
        int dy=y-cy,half=radius;
        while(half*half+dy*dy>radius*radius)--half;
        fill(cx-half,y,half*2+1,1,color);
    }
}

static void ellipse(int cx,int cy,int rx,int ry,uint16_t color) {
    int top=cy-ry,bottom=cy+ry;
    if(top<strip_y)top=strip_y;if(bottom>=strip_y+8)bottom=strip_y+7;
    for(int y=top;y<=bottom;++y) {
        int dy=(y-cy)*rx/ry,half=rx;
        while(half*half+dy*dy>rx*rx)--half;
        fill(cx-half,y,half*2+1,1,color);
    }
}

static void stay_puft(const game_t *g) {
    int rise=g->boss_frame<GAME_BOSS_ENTRANCE?
             (GAME_BOSS_ENTRANCE-(int)g->boss_frame)*3:0;
    int cx=187+(g->boss_flash?1:0),cy=112+rise;
    if(!g->boss_defeat || g->boss_defeat<18) {
        uint16_t shade=g->boss_flash?WHITE:0xa65c;
        uint16_t body=g->boss_flash?WHITE:0xe77e;
        /* Rounded marshmallow segments and a cool shadow along the right edge. */
        ellipse(cx-17,cy+126,18,15,shade);ellipse(cx+20,cy+126,18,15,shade);
        ellipse(cx,cy+83,39,43,shade);ellipse(cx-5,cy+78,34,39,body);
        ellipse(cx-5,cy+64,28,15,WHITE);
        fill(cx-28,cy+89,55,2,0xc6bd);fill(cx-29,cy+108,55,2,0xc6bd);
        int wave=(g->boss_frame/15)%2?2:0;
        ellipse(cx-42,cy+53+wave,17,22,shade);
        ellipse(cx-45,cy+48+wave,14,18,body);
        ellipse(cx-49,cy+72+wave,13,12,WHITE);
        ellipse(cx+35,cy+55,14,22,shade);
        ellipse(cx+37,cy+76,12,12,body);
        ellipse(cx,cy,31,32,shade);ellipse(cx-4,cy-3,28,29,body);
        ellipse(cx-11,cy-12,15,13,WHITE);
        /* Blue sailor collar, white piping, and red neckerchief. */
        fill(cx-27,cy+28,54,9,0x1931);fill(cx-22,cy+37,44,5,0x1931);
        fill(cx-25,cy+29,50,2,CYAN);fill(cx-10,cy+40,20,3,WHITE);
        fill(cx-4,cy+36,8,9,RED);fill(cx-8,cy+45,7,13,RED);
        fill(cx+2,cy+45,7,10,0xb8a6);
        /* Hat crown, dark ribbon and side tassel. */
        ellipse(cx-2,cy-34,24,10,WHITE);fill(cx-28,cy-31,56,7,0x1931);
        fill(cx-24,cy-25,49,3,WHITE);fill(cx-29,cy-29,5,15,0x1931);
        text(cx-18,cy-30,"STAY PUFT",1,GOLD);
        fill(cx-14,cy-4,5,6,INK);fill(cx+9,cy-4,5,6,INK);
        fill(cx-13,cy-4,2,2,WHITE);fill(cx+10,cy-4,2,2,WHITE);
        if(g->boss_hp<=GAME_BOSS_HP/2) {
            fill(cx-17,cy-9,10,2,INK);fill(cx+7,cy-9,10,2,INK);
            fill(cx-7,cy+15,14,4,INK);fill(cx-3,cy+16,6,2,RED);
        } else {
            fill(cx-9,cy+12,3,3,INK);fill(cx+7,cy+12,3,3,INK);
            fill(cx-7,cy+15,14,2,INK);
        }
        unsigned burns=(GAME_BOSS_HP-g->boss_hp)/3;
        for(unsigned i=0;i<burns;++i) {
            int x=cx-20+(int)(i*17%42),y=cy+62+(int)(i*13%45);
            ellipse(x,y,5,3,0x7b49);fill(x-2,y-1,3,1,0xbcef);
        }
    }
    if(g->boss_flash) {
        int y=lane_y(g->lane);
        fill(158,y-6,2,13,GOLD);fill(153,y,13,2,WHITE);
        fill(155,y-8,2,2,CYAN);fill(164,y+7,2,2,GOLD);
    }
    if(g->boss_defeat) {
        static const int dx[10]={-3,-2,-1,0,1,2,3,-2,0,2};
        static const int dy[10]={-1,-2,-3,-2,-3,-2,-1,1,2,1};
        int r=(int)g->boss_defeat/2;
        for(unsigned i=0;i<10;++i) {
            int x=183+dx[i]*r/2,y=162+dy[i]*r/2;
            ellipse(x,y,7,5,0x9e3b);ellipse(x-1,y-1,5,3,WHITE);
        }
        if(g->boss_defeat>18) {
            centered(128,"ROASTED",3,GOLD);centered(151,"1500 POINTS",2,WHITE);
        }
    }
}

static void no_ghost_emblem(void) {
    if(strip_y>=172 || strip_y+8<=40)return;
    static const char *const emblem[]={
        "      WWWW      ","    WWWWWWWW    ","   WWWWWWWWWW   ",
        "  WWWWWWWWWWWs  ","  WWWWWWWWWWWs  ","  WWDDWWDDWWWs  ",
        "  WWDDWWDDWWWs  ","  WWWWWWWWWWWs  ","  WWWWDDWWWWWs  ",
        " WWWWWDDWWWWWss ","WWWWWWWWWWWWWsss","WWWWWWWWWWWWWsss",
        " ssWWWWWWWWWWss ","  sWWWWWWWWWss  ","  WWWWWWWWWWss  ",
        " WWWWWWWWWWWsss "," WWssWWWssWWWss "," Ws  WWs  WWss  ",
        " s   Ws   Ws    ","     s    s     "};
    /* Oversized shaded no-ghost emblem, hand-drawn at four-pixel resolution. */
    disc(120,106,64,0x1069);disc(120,106,61,0x4808);
    disc(120,104,59,RED);disc(120,104,50,INK);
    for(unsigned row=0;row<20;++row) {
    if(59+(int)row*4>=strip_y+8 || 63+(int)row*4<=strip_y)continue;
    for(unsigned col=0;col<16;++col) {
        char c=emblem[row][col];if(c==' ')continue;
        fill(88+(int)col*4,59+(int)row*4,4,4,
             c=='W'?WHITE:(c=='s'?0x9e3b:INK));
    }
    }
    /* Hands curl over the ring; the slash remains in front of the ghost. */
    fill(69,100,14,9,WHITE);fill(72,109,8,4,0x9e3b);
    fill(157,100,14,9,WHITE);fill(160,109,8,4,0x9e3b);
    for(int i=0;i<80;++i) {
        if(61+i>=strip_y+8 || 72+i<=strip_y)continue;
        fill(77+i,62+i,11,10,0xb8a6);
        fill(77+i,61+i,9,8,RED);
        fill(78+i,61+i,2,2,0xfbae);
    }
}

static void poster_background(void) {
    for(int y=strip_y;y<strip_y+8;++y) {
        unsigned blue=5+(unsigned)(320-y)/48;
        fill(0,y,240,1,(uint16_t)(0x0800|blue));
    }
    for(unsigned i=0;i<48;++i) {
        int x=5+(i*73)%230,y=7+(i*47)%306;
        fill(x,y,i%7==0?2:1,1,i%3?0x29ad:0x53b3);
    }
    fill(0,0,240,3,RED);fill(0,317,240,3,RED);
}

static void title_scene(const game_t *g) {
    poster_background();
    centered(17,"WHO YOU GONNA CALL",2,CYAN);
    no_ghost_emblem();
    centered(179,"GHOST",5,0x1b4c);centered(176,"GHOST",5,WHITE);
    centered(210,"BUSTERS",5,0x1b4c);centered(207,"BUSTERS",5,WHITE);
    draw_ghost(12,192,0,g->frame,0);draw_ghost(205,218,1,g->frame,1);
    fill(0,249,240,2,0x1a47);fill(0,251,240,1,GREEN);
    for(int x=0;x<240;x+=17)fill(x,252,7,2+(x%5),0x4509);
    fill(18,267,204,24,PANEL);fill(18,267,204,1,GREEN);
    centered(274,"SPEAK TO START",2,GREEN);
    centered(299,"HUM TO MOVE / SPEAK TO FIRE",1,MUTED);
    centered(309,"STAY PUFT IS COMING",1,CYAN);
}

static void game_over_scene(const game_t *g) {
    poster_background();no_ghost_emblem();
    centered(16,"GHOSTBUSTERS",2,WHITE);
    centered(32,"PARANORMAL CONTAINMENT DIVISION",1,MUTED);

    centered(179,"GAME OVER",5,0x4808);
    centered(176,"GAME OVER",5,WHITE);
    centered(207,"THE CITY NEEDS YOU",1,CYAN);

    fill(16,222,208,48,0x294b);fill(17,223,206,46,PANEL);
    fill(119,231,1,31,0x294b);
    text(28,231,"FINAL SCORE",1,MUTED);text(138,231,"BEST SCORE",1,MUTED);
    char label[24];
    /* Fit even the full unsigned score range inside each half of the panel. */
    unsigned size=g->score>999999?1:2;
    snprintf(label,sizeof(label),"%u",g->score);text(28,246,label,size,GOLD);
    size=g->best>999999?1:2;
    snprintf(label,sizeof(label),"%u",g->best);text(138,246,label,size,GREEN);
    if(g->restart_ready) {
        fill(18,282,204,22,0x1a88);fill(18,282,204,1,GREEN);
        centered(288,"SPEAK TO RESTART",2,GREEN);
        centered(309,"ONE MORE CALL",1,MUTED);
    } else {
        centered(283,"BE QUIET A MOMENT",2,WHITE);
        centered(301,"THEN SPEAK TO TRY AGAIN",1,MUTED);
        fill(40,312,160,2,0x294b);
        fill(40,312,(int)(g->quiet>30?30:g->quiet)*160/30,2,CYAN);
    }
}

static void draw_scene(const game_t *g) {
    if(g->title){title_scene(g);return;}
    if(g->over){game_over_scene(g);return;}
    char label[32];
    fill(0,strip_y,240,8,BG);
    if(strip_y>=56 && strip_y<272) {
    /* Static layered city: only actors and effects dirty strips during play. */
    fill(0,56,240,72,0x0844);fill(0,128,240,68,0x1065);
    fill(0,196,240,66,0x1086);
    fill(193,63,12,2,0x6b91);fill(189,65,20,12,0x6b91);
    fill(193,77,12,2,0x6b91);fill(197,63,14,10,0x0844);
    for(unsigned i=0;i<17;++i) {
        int x=9+(i*47)%224,y=62+(i*19)%65;
        fill(x,y,1,1,i%3?0x39cd:0x7bf3);
    }
    for(unsigned i=0;i<11;++i) {
        int h=42+(i*29)%93,x=(int)i*25-7;
        fill(x,255-h,21,h,0x18a8);
        fill(x+4,251-h,12,4,0x18a8);
        fill(x+10,244-h,1,7,0x294b);
        int first=262-h;
        if(strip_y>first+3)first+=((strip_y-first-4)/10+1)*10;
        for(int y=first;y<252 && y<strip_y+8;y+=10)for(int col=0;col<2;++col)
            if((i+(unsigned)y+(unsigned)col)%4)
                fill(x+4+col*8,y,3,4,((i+col)%3)?0x294b:0x526d);
    }
    for(unsigned i=0;i<6;++i) {
        int x=(int)i*45-10,h=25+(i*17)%39;
        fill(x,260-h,30,h,0x0864);fill(x-2,260-h,34,2,0x2129);
        int first=266-h;
        if(strip_y>first+2)first+=((strip_y-first-3)/9+1)*9;
        for(int y=first;y<253 && y<strip_y+8;y+=9) {
            fill(x+5,y,4,3,0x324a);fill(x+20,y,4,3,0x324a);
        }
    }
    for(unsigned lane=0;lane<3;++lane) {
        int y=lane_y(lane);
        fill(6,y+25,228,1,0x29ac);fill(6,y+26,228,1,INK);
        for(int x=64;x<240;x+=24)fill(x,y+24,10,1,0x426e);
        fill(3,y-17,2,34,lane==g->lane?GREEN:0x2188);
        if(lane==g->lane) {
            fill(7,y-2,2,5,GREEN);fill(9,y-1,2,3,GREEN);fill(11,y,2,1,WHITE);
            fill(24,y+21,34,1,0x342b);
        }
    }
    if(g->boss_active)stay_puft(g);
    fill(0,260,240,4,0x1a47);fill(0,262,240,1,GREEN);
    for(int x=0;x<240;x+=19) {
        fill(x,263,9,2,0x4509);fill(x+3,265,3,2,0x3428);
    }
    }
    if(strip_y<56) {
    fill(0,0,240,48,PANEL);fill(0,0,240,2,RED);
    fill(0,47,240,1,0x31ac);
    text(33,10,"GHOSTBUSTERS",3,INK);text(32,8,"GHOSTBUSTERS",3,WHITE);
    /* Red no-ghost badge. */
    fill(10,6,12,2,RED);fill(6,8,20,3,RED);fill(4,11,24,13,RED);
    fill(6,24,20,3,RED);fill(10,27,12,2,RED);
    fill(10,10,12,14,INK);fill(7,13,18,8,INK);
    fill(13,10,8,3,WHITE);fill(11,13,12,8,WHITE);
    fill(9,19,16,3,WHITE);fill(14,14,2,3,INK);fill(19,14,2,3,INK);
    for(int i=0;i<10;++i)fill(6+i*2,8+i*2,3,3,RED);
    snprintf(label,sizeof(label),"SCORE %06u",g->score);
    text(8,33,label,2,GREEN);
    text(156,34,"LIVES",1,WHITE);
    for(unsigned i=0;i<3;++i) {
        int x=184+(int)i*17;
        uint16_t c=i<g->lives?RED:0x2947;
        fill(x,32,4,4,c);fill(x+6,32,4,4,c);fill(x,36,10,3,c);
        fill(x+2,39,6,2,c);fill(x+4,41,2,2,c);
    }
    }
    if(strip_y>=48 && strip_y<264) {
    if(g->boss_active) {
        text(8,52,g->boss_defeat?"STAY PUFT DEFEATED":"STAY PUFT",1,WHITE);
        fill(92,51,140,7,INK);fill(93,52,(int)g->boss_hp*138/GAME_BOSS_HP,5,RED);
        if(g->boss_frame<GAME_BOSS_ENTRANCE) {
            fill(18,142,120,29,PANEL);text(26,149,"HE IS HERE",2,GOLD);
            text(26,162,"STAY PUFT",1,WHITE);
        }
        if(g->boss_attack>60 && !g->boss_defeat) {
            int y=lane_y(g->boss_lane);
            for(int x=64;x<154;x+=12)fill(x,y+12,6,2,RED);
            text(74,y-23,"DODGE",1,GOLD);
        }
    } else text(8,52,"HIGH",1,g->lane==0?GREEN:MUTED);
    text(8,120,"MID",1,g->lane==1?GREEN:MUTED);
    text(8,188,"LOW",1,g->lane==2?GREEN:MUTED);
    for(unsigned i=0;i<GAME_GHOSTS;++i)if(g->ghosts[i].active) {
        const game_ghost_t *ghost=&g->ghosts[i];
        /* Sprite and glow stay close to the original collision silhouette. */
        draw_ghost(ghost->x-12,lane_y(ghost->lane)-14,ghost->kind,g->frame,i);
    }
    for(unsigned i=0;i<GAME_SHOTS;++i)if(g->shots[i].active) {
        int x=g->shots[i].x,y=lane_y(g->shots[i].lane);
        bolt(x,y,g->frame+i);
    }
    for(unsigned i=0;i<GAME_HAZARDS;++i)if(g->hazards[i].active) {
        int x=g->hazards[i].x,y=lane_y(g->hazards[i].lane);
        fill(x+8,y-2,7,4,0x526d);fill(x+17,y,4,1,0x294b);
        ellipse(x,y,8,7,0xa65c);ellipse(x-2,y-2,6,4,WHITE);
        fill(x-3,y+4,7,1,0x7bd1);
    }
    for(unsigned i=0;i<GAME_EFFECTS;++i)if(g->effects[i].age)capture(&g->effects[i]);
    if(!g->invincible || (g->frame/3)%2)
        sprite(26,lane_y(g->lane)-14,buster_sprite,14,SUIT,0x8b48,0);
    fill(46,lane_y(g->lane)-2,11,4,0x7bf0);
    fill(48,lane_y(g->lane)-2,8,1,WHITE);
    fill(55,lane_y(g->lane)-3,3,6,INK);
    if(g->voice && g->cooldown>7) {
        int y=lane_y(g->lane);
        fill(58,y-3,5,7,GOLD);fill(59,y-1,7,3,WHITE);
        fill(61,y-6,2,2,CYAN);fill(61,y+6,2,2,CYAN);
        fill(23,y-5,2,2,CYAN);fill(21,y+1,2,4,0x2375);
        fill(24,y+7,2,2,GOLD);
    }
    if(g->hit_flash){fill(0,60,240,2,RED);fill(0,258,240,2,RED);}
    }
    if(strip_y>=264) {
    fill(0,270,240,50,PANEL);fill(0,270,240,1,0x31ac);
    text(8,277,"HUM TO DODGE",2,WHITE);
    text(8,292,"LOW / MID / HIGH",1,MUTED);
    text(8,305,"SPEAK TO FIRE",2,g->voice?GOLD:MUTED);
    if(g->pitch_hz)snprintf(label,sizeof(label),"%u HZ",g->pitch_hz);
    else snprintf(label,sizeof(label),"HUM --");
    text(174,277,label,1,CYAN);
    text(174,290,g->voice?"FIRING":(g->hum?"HUM":"QUIET"),1,
         g->voice?GOLD:(g->hum?CYAN:MUTED));
    for(unsigned i=0;i<9;++i) {
        int height=2+(int)((i*7)%9);
        unsigned active=(g->voice||g->hum) && i<(g->voice?8:3+g->lane*2);
        fill(174+i*6,314-height,3,height,active?(g->voice?GOLD:CYAN):0x294a);
    }
    }
    if(g->frame<90 && !g->over) {
        fill(19,142,206,37,INK);fill(15,137,210,39,0x342b);
        fill(17,139,206,35,PANEL);text(32,146,"WHO YOU GONNA",2,WHITE);
        text(76,161,"CALL",2,GREEN);
    }
}
/* Exact changed-column bounds across a strip keep narrow animations off SPI. */
static int changed_columns(const uint16_t *previous,const uint16_t *current,
                           int *left,int *right) {
    if(!memcmp(previous,current,240*8*sizeof(uint16_t)))return 0;
    *left=240;*right=0;
    for(int row=0;row<8;++row) {
        int base=row*240;
        for(int x=0;x<*left;++x)if(previous[base+x]!=current[base+x]){*left=x;break;}
        for(int x=239;x>=*right;--x)if(previous[base+x]!=current[base+x]){*right=x+1;break;}
        if(!*left && *right==240)break;
    }
    return 1;
}

#ifndef DISPLAY_HOST_PREVIEW
static void render(const game_t *current,unsigned first) {
    for(strip_y=0;strip_y<320;strip_y+=8) {
        canvas=strip[1];draw_scene(current);
        uint16_t *previous=&previous_frame[strip_y*240];
        int left=0,right=240;
        if(!first && !changed_columns(previous,canvas,&left,&right))continue;
        memcpy(previous,canvas,sizeof(strip[1]));
        area(left,strip_y,right-left,8);
        for(unsigned row=0;row<8;++row) {
            for(int x=left;x<right;++x) {
                uint16_t c=canvas[row*240+x];
                pixels[2*(x-left)]=c>>8;pixels[2*(x-left)+1]=c;
            }
            spi_master_transfer(&lcd,pixels,NULL,(right-left)*2);
        }
        spi_master_end_transaction(&lcd);
    }
}
void display_task(unsigned c_display) {
    port_enable(PORT_LCD_DC);port_enable(PORT_LCD_RST);
    port_out(PORT_LCD_DC,1);port_out(PORT_LCD_RST,1);wait_ms(10);
    spi_master_init(&bus,XS1_CLKBLK_1,PORT_LCD_CS,PORT_LCD_SCLK,PORT_LCD_MOSI,0);
    // 600 MHz / (4 * 5) = 30 MHz. No MISO port is allocated.
    spi_master_device_init(&lcd,&bus,0,0,0,spi_master_source_clock_xcore,5,
        spi_master_sample_delay_1_2,0,20,20,20);
    port_out(PORT_LCD_RST,0);wait_ms(20);port_out(PORT_LCD_RST,1);wait_ms(120);
    command(0x01,NULL,0);wait_ms(150);command(0x11,NULL,0);wait_ms(120);
    uint8_t format=0x55,rotation=0;
    command(0x3a,&format,1);command(0x36,&rotation,1);
    command(0x21,NULL,0);command(0x13,NULL,0);command(0x29,NULL,0);wait_ms(20);
    lcd_fill(0,0,240,320,BG);
    game_t game;
    game_init(&game,get_reference_time());
    unsigned first=1;
    uint32_t packet[SPECTRUM_WORDS];
    hwtimer_t timer=hwtimer_alloc();
    while(1) {
        unsigned start=get_reference_time();
        if(display_reset_stats) {
            display_peak_ticks=0;display_budget_misses=0;display_reset_stats=0;
        }
        chan_out_word(c_display,1);
        for(unsigned i=0;i<SPECTRUM_WORDS;++i) packet[i]=chan_in_word(c_display);
        display_last_spectrum=packet[BARS];
        game_step(&game,packet[BARS+7]?packet[BARS+6]:0,packet[BARS+8]);
        game.hum=packet[BARS+9];
        render(&game,first);first=0;
        game_diag=game;
        ++display_frames;display_frame_ticks=get_reference_time()-start;
        if(display_frame_ticks>display_peak_ticks)display_peak_ticks=display_frame_ticks;
        if(display_frame_ticks>3333333)++display_budget_misses;
        unsigned now=get_reference_time();
        if((unsigned)(now-start)<3333333)hwtimer_wait_until(timer,start+3333333);
    }
}
#endif
