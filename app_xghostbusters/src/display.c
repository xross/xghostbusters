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
#endif
/* Two eight-row strips allow dirty redraws without a full-frame framebuffer. */
static uint16_t strip[2][240*8] __attribute__((aligned(8)));
static uint16_t *canvas;
static int strip_y;
#ifndef DISPLAY_HOST_PREVIEW
volatile game_t game_diag;
volatile unsigned display_frames, display_last_spectrum, display_frame_ticks;
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
    for(unsigned row=0;row<height;++row)for(unsigned col=0;rows[row][col];++col) {
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
    static const int wave[8]={0,-2,-3,0,3,1,-2,0};
    for(int i=0;i<8;++i) {
        int offset=wave[(i+frame/2)%8];
        fill(x+i*2,y+offset-2,3,5,0xa268);
        fill(x+i*2,y+offset-1,3,3,GOLD);
        fill(x+i*2,y+offset,2,1,WHITE);
        fill(x+i*2,y-offset+5,2,1,CYAN);
    }
    fill(x+15,y-1,3,3,WHITE);
}

static void capture(const game_effect_t *e) {
    int x=e->x,y=lane_y(e->lane),r=3+(int)e->age;
    uint16_t color=ghost_colors[e->kind];
    static const int dx[8]={-2,-1,0,1,2,1,0,-1};
    static const int dy[8]={0,-1,-2,-1,0,1,2,1};
    for(unsigned i=0;i<8;++i) {
        int size=e->age<7?2:1;
        fill(x+dx[i]*r/2,y+dy[i]*r/2,size,size,i&1?color:WHITE);
    }
    if(e->age<5) {
        fill(x-4,y,9,1,WHITE);fill(x,y-4,1,9,WHITE);
    }
    char points[8];snprintf(points,sizeof(points),"%u",100u<<e->kind);
    int tx=x-6;if(tx<2)tx=2;if(tx>226)tx=226;
    text(tx,y-17-e->age/2,points,1,color);
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

static void game_over_scene(const game_t *g) {
    static const char *const emblem[]={
        "      WWWW      ","    WWWWWWWW    ","   WWWWWWWWWW   ",
        "  WWWWWWWWWWWs  ","  WWWWWWWWWWWs  ","  WWDDWWDDWWWs  ",
        "  WWDDWWDDWWWs  ","  WWWWWWWWWWWs  ","  WWWWDDWWWWWs  ",
        " WWWWWDDWWWWWss ","WWWWWWWWWWWWWsss","WWWWWWWWWWWWWsss",
        " ssWWWWWWWWWWss ","  sWWWWWWWWWss  ","  WWWWWWWWWWss  ",
        " WWWWWWWWWWWsss "," WWssWWWssWWWss "," Ws  WWs  WWss  ",
        " s   Ws   Ws    ","     s    s     "};
    /* Edge-to-edge midnight poster, kept static while waiting for a restart. */
    for(int y=strip_y;y<strip_y+8;++y) {
        unsigned blue=5+(unsigned)(320-y)/48;
        fill(0,y,240,1,(uint16_t)(0x0800|blue));
    }
    for(unsigned i=0;i<48;++i) {
        int x=5+(i*73)%230,y=7+(i*47)%306;
        fill(x,y,i%7==0?2:1,1,i%3?0x29ad:0x53b3);
    }
    fill(0,0,240,3,RED);fill(0,317,240,3,RED);
    centered(16,"GHOSTBUSTERS",2,WHITE);
    centered(32,"PARANORMAL CONTAINMENT DIVISION",1,MUTED);

    /* Oversized shaded no-ghost emblem, hand-drawn at four-pixel resolution. */
    disc(120,106,64,0x1069);disc(120,106,61,0x4808);
    disc(120,104,59,RED);disc(120,104,50,INK);
    for(unsigned row=0;row<20;++row)for(unsigned col=0;col<16;++col) {
        char c=emblem[row][col];if(c==' ')continue;
        fill(88+(int)col*4,59+(int)row*4,4,4,
             c=='W'?WHITE:(c=='s'?0x9e3b:INK));
    }
    /* Hands curl over the ring; the slash remains in front of the ghost. */
    fill(69,100,14,9,WHITE);fill(72,109,8,4,0x9e3b);
    fill(157,100,14,9,WHITE);fill(160,109,8,4,0x9e3b);
    for(int i=0;i<80;++i) {
        fill(77+i,62+i,11,10,0xb8a6);
        fill(77+i,61+i,9,8,RED);
        fill(78+i,61+i,2,2,0xfbae);
    }

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
        for(int y=262-h;y<252;y+=10)for(int col=0;col<2;++col)
            if((i+(unsigned)y+(unsigned)col)%4)
                fill(x+4+col*8,y,3,4,((i+col)%3)?0x294b:0x526d);
    }
    for(unsigned i=0;i<6;++i) {
        int x=(int)i*45-10,h=25+(i*17)%39;
        fill(x,260-h,30,h,0x0864);fill(x-2,260-h,34,2,0x2129);
        for(int y=266-h;y<253;y+=9) {
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
    text(8,52,"HIGH",1,g->lane==0?GREEN:MUTED);
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
#ifndef DISPLAY_HOST_PREVIEW
static void render(const game_t *old,const game_t *current,unsigned first) {
    for(strip_y=0;strip_y<320;strip_y+=8) {
        canvas=strip[0];draw_scene(old);
        canvas=strip[1];draw_scene(current);
        if(!first && !memcmp(strip[0],strip[1],sizeof(strip[0])))continue;
        area(0,strip_y,240,8);
        for(unsigned row=0;row<8;++row) {
            for(unsigned x=0;x<240;++x) {
                uint16_t c=strip[1][row*240+x];pixels[2*x]=c>>8;pixels[2*x+1]=c;
            }
            spi_master_transfer(&lcd,pixels,NULL,sizeof(pixels));
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
    game_t game,old;
    game_init(&game,get_reference_time());old=game;
    unsigned first=1;
    uint32_t packet[SPECTRUM_WORDS];
    hwtimer_t timer=hwtimer_alloc();
    while(1) {
        unsigned start=get_reference_time();
        chan_out_word(c_display,1);
        for(unsigned i=0;i<SPECTRUM_WORDS;++i) packet[i]=chan_in_word(c_display);
        display_last_spectrum=packet[BARS];
        game_step(&game,packet[BARS+7]?packet[BARS+6]:0,packet[BARS+8]);
        game.hum=packet[BARS+9];
        render(&old,&game,first);old=game;first=0;
        game_diag=game;
        ++display_frames;display_frame_ticks=get_reference_time()-start;
        unsigned now=get_reference_time();
        if((unsigned)(now-start)<3333333)hwtimer_wait_until(timer,start+3333333);
    }
}
#endif
