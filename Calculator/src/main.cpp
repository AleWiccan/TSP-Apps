/*
 * calculator.c — Calculadora para Trimui Smart Pro (TG5040)
 *
 * Pantalla física: 1280×720 (IPS, landscape)
 * CPU: Allwinner A133+, 4× ARM Cortex-A53 (aarch64)
 * Libs: SDL2, SDL2_ttf  (en /usr/trimui/lib del dispositivo)
 *
 * Controles:
 *   D-Pad / Analog stick → navegar
 *   A → seleccionar botón
 *   B → borrar último dígito
 *   START → AC (limpiar)
 *   SELECT → salir
 */
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── Resolución nativa de la Trimui Smart Pro ── */
#define SCREEN_W  1280
#define SCREEN_H   720

/* ── Layout ──
 * Pantalla en landscape: display a la izquierda, grid a la derecha.
 *
 *  [  DISPLAY (550×680)  ] [ GRID 4×5 (680×680) ]
 */
#define DISP_X    20
#define DISP_Y    20
#define DISP_W   570
#define DISP_H   680

#define GRID_X   620
#define GRID_Y    20
#define BTN_W    152
#define BTN_H    128
#define BTN_GAP   10

#define FONT_LG   96
#define FONT_SM   48
#define FONT_BTN  38
#define FONT_HIST 26

typedef enum { BT_NUM, BT_OP, BT_FN, BT_EQ } BtnType;

typedef struct {
    const char *label, *action, *value;
    BtnType type;
    int col, row, colspan;
} Button;

static const Button BUTTONS[] = {
    {"AC",  "clear","",   BT_FN, 0,0,1},
    {"+/-", "sign", "",   BT_FN, 1,0,1},
    {"%",   "pct",  "",   BT_FN, 2,0,1},
    {"\xC3\xB7","op","÷", BT_OP, 3,0,1},
    {"7","digit","7",BT_NUM,0,1,1},{"8","digit","8",BT_NUM,1,1,1},
    {"9","digit","9",BT_NUM,2,1,1},{"\xC3\x97","op","×",BT_OP,3,1,1},
    {"4","digit","4",BT_NUM,0,2,1},{"5","digit","5",BT_NUM,1,2,1},
    {"6","digit","6",BT_NUM,2,2,1},{"\xe2\x88\x92","op","−",BT_OP,3,2,1},
    {"1","digit","1",BT_NUM,0,3,1},{"2","digit","2",BT_NUM,1,3,1},
    {"3","digit","3",BT_NUM,2,3,1},{"+","op","+",BT_OP,3,3,1},
    {"0","digit","0",BT_NUM,0,4,2},
    {".","dot",  "", BT_NUM,2,4,1},
    {"=","eq",   "", BT_EQ, 3,4,1},
};
#define NUM_BUTTONS (int)(sizeof(BUTTONS)/sizeof(BUTTONS[0]))

typedef struct {
    char current[32], previous[32], op[4], history[128];
    int  should_reset, is_error;
} CalcState;

static double compute(double a,const char *op,double b){
    if(!strcmp(op,"+")) return a+b;
    if(!strcmp(op,"−")) return a-b;
    if(!strcmp(op,"×")) return a*b;
    if(!strcmp(op,"÷")) return b==0?NAN:a/b;
    return b;
}
static void fmtnum(double v,char *o,size_t s){
    if(isnan(v)||isinf(v)){snprintf(o,s,"Error");return;}
    if(fmod(v,1.0)==0&&fabs(v)<1e12) snprintf(o,s,"%.0f",v);
    else snprintf(o,s,"%.10g",v);
}
static void calc_press(CalcState *s,const Button *b){
    const char *a=b->action;
    if(!strcmp(a,"clear")){
        strcpy(s->current,"0");strcpy(s->previous,"");
        strcpy(s->op,"");strcpy(s->history,"");
        s->should_reset=s->is_error=0;return;
    }
    if(s->is_error) return;
    if(!strcmp(a,"digit")){
        if(s->should_reset||!strcmp(s->current,"0")){
            strncpy(s->current,b->value,31);s->current[31]=0;
            s->should_reset=0;
        } else if(strlen(s->current)<12)
            strncat(s->current,b->value,31-strlen(s->current));
        return;
    }
    if(!strcmp(a,"dot")){
        if(s->should_reset){strcpy(s->current,"0.");s->should_reset=0;return;}
        if(!strchr(s->current,'.')) strncat(s->current,".",31-strlen(s->current));
        return;
    }
    if(!strcmp(a,"sign")){fmtnum(-atof(s->current),s->current,32);return;}
    if(!strcmp(a,"pct")){ fmtnum(atof(s->current)/100.0,s->current,32);return;}
    if(!strcmp(a,"op")){
        if(strlen(s->op)&&!s->should_reset){
            double r=compute(atof(s->previous),s->op,atof(s->current));
            if(isnan(r)||isinf(r)){strcpy(s->current,"Error");s->is_error=1;return;}
            fmtnum(r,s->current,32);
        }
        strncpy(s->previous,s->current,30);s->previous[30]=0;
        strncpy(s->op,b->value,3);s->op[3]=0;
        snprintf(s->history,127,"%s %s",s->previous,s->op);
        s->should_reset=1;return;
    }
    if(!strcmp(a,"eq")){
        if(!strlen(s->op)) return;
        double r=compute(atof(s->previous),s->op,atof(s->current));
        snprintf(s->history,127,"%s %s %s =",s->previous,s->op,s->current);
        if(isnan(r)||isinf(r)){strcpy(s->current,"Error");s->is_error=1;}
        else fmtnum(r,s->current,32);
        strcpy(s->previous,"");strcpy(s->op,"");s->should_reset=1;return;
    }
}

/* ── Render helpers ── */
static void sc(SDL_Renderer *r,SDL_Color c){SDL_SetRenderDrawColor(r,c.r,c.g,c.b,c.a);}
static void border(SDL_Renderer *r,int x,int y,int w,int h,SDL_Color c){
    sc(r,c);SDL_Rect rc={x,y,w,h};SDL_RenderDrawRect(r,&rc);
}
static void rounded(SDL_Renderer *r,int x,int y,int w,int h,int rad,SDL_Color c){
    sc(r,c);
    SDL_Rect ra={x+rad,y,w-2*rad,h},rb={x,y+rad,rad,h-2*rad},rc2={x+w-rad,y+rad,rad,h-2*rad};
    SDL_RenderFillRect(r,&ra);SDL_RenderFillRect(r,&rb);SDL_RenderFillRect(r,&rc2);
    for(int dy=0;dy<=rad;dy++) for(int dx=0;dx<=rad;dx++) if(dx*dx+dy*dy<=rad*rad){
        SDL_RenderDrawPoint(r,x+rad-dx,     y+rad-dy);
        SDL_RenderDrawPoint(r,x+w-rad+dx-1, y+rad-dy);
        SDL_RenderDrawPoint(r,x+rad-dx,     y+h-rad+dy-1);
        SDL_RenderDrawPoint(r,x+w-rad+dx-1, y+h-rad+dy-1);
    }
}
static void txtright(SDL_Renderer *r,TTF_Font *f,const char *t,SDL_Color c,int rx,int cy){
    SDL_Surface *s=TTF_RenderUTF8_Blended(f,t,c);if(!s)return;
    SDL_Texture *tx=SDL_CreateTextureFromSurface(r,s);
    SDL_Rect d={rx-s->w,cy-s->h/2,s->w,s->h};SDL_FreeSurface(s);
    SDL_RenderCopy(r,tx,NULL,&d);SDL_DestroyTexture(tx);
}
static void txtcenter(SDL_Renderer *r,TTF_Font *f,const char *t,SDL_Color c,int cx,int cy){
    SDL_Surface *s=TTF_RenderUTF8_Blended(f,t,c);if(!s)return;
    SDL_Texture *tx=SDL_CreateTextureFromSurface(r,s);
    SDL_Rect d={cx-s->w/2,cy-s->h/2,s->w,s->h};SDL_FreeSurface(s);
    SDL_RenderCopy(r,tx,NULL,&d);SDL_DestroyTexture(tx);
}

static SDL_Rect btn_rect(int i){
    const Button *b=&BUTTONS[i];
    return (SDL_Rect){
        GRID_X+b->col*(BTN_W+BTN_GAP),
        GRID_Y+b->row*(BTN_H+BTN_GAP),
        b->colspan*BTN_W+(b->colspan-1)*BTN_GAP,
        BTN_H
    };
}
static void draw_btn(SDL_Renderer *r,TTF_Font *f,int i,int foc){
    const Button *b=&BUTTONS[i];SDL_Rect rc=btn_rect(i);
    SDL_Color bg,tc;
    SDL_Color NUM={19,22,30,255},OP={20,18,10,255},FN={20,14,10,255};
    SDL_Color EQ={6,214,160,255},FOC={18,28,45,255};
    SDL_Color CTEXT={232,234,240,255},COP={255,209,102,255},CFN={255,107,53,255};
    SDL_Color CEQ={10,12,16,255},ACC={0,229,255,255},BDR={30,35,48,255};
    switch(b->type){
        case BT_NUM:bg=foc?FOC:NUM;tc=CTEXT;break;
        case BT_OP: bg=foc?FOC:OP; tc=COP;  break;
        case BT_FN: bg=foc?FOC:FN; tc=CFN;  break;
        case BT_EQ: bg=EQ;         tc=CEQ;  break;
    }
    rounded(r,rc.x,rc.y,rc.w,rc.h,10,bg);
    border(r,rc.x,rc.y,rc.w,rc.h,foc?ACC:BDR);
    txtcenter(r,f,b->label,tc,rc.x+rc.w/2,rc.y+rc.h/2);
}
static void draw_display(SDL_Renderer *r,TTF_Font *flg,TTF_Font *fsm,TTF_Font *fh,const CalcState *s){
    SDL_Color PANEL={17,19,24,255},BDR={30,35,48,255};
    SDL_Color TEXT={232,234,240,255},MUTED={74,80,104,255};
    SDL_Color ERR={255,107,53,255},ACC={0,229,255,255};
    rounded(r,DISP_X,DISP_Y,DISP_W,DISP_H,14,PANEL);
    border(r,DISP_X,DISP_Y,DISP_W,DISP_H,BDR);
    sc(r,ACC);
    SDL_RenderDrawLine(r,DISP_X+40,DISP_Y+1,DISP_X+DISP_W-40,DISP_Y+1);
    /* título */
    txtcenter(r,fh,"CALCULADORA",MUTED,DISP_X+DISP_W/2,DISP_Y+28);
    /* historial */
    if(strlen(s->history))
        txtright(r,fh,s->history,MUTED,DISP_X+DISP_W-22,DISP_Y+90);
    /* número */
    SDL_Color mc=s->is_error?ERR:TEXT;
    TTF_Font *fm=(strlen(s->current)>10)?fsm:flg;
    txtright(r,fm,s->current,mc,DISP_X+DISP_W-22,DISP_Y+DISP_H/2+40);
    /* hints controles */
    SDL_Color H={50,55,75,255};
    txtcenter(r,fh,"A:OK  B:Del  Y:AC  X:Salir",H,DISP_X+DISP_W/2,DISP_Y+DISP_H-30);
}

static int nav_move(int idx,int dir){
    const Button *cur=&BUTTONS[idx];
    int best=-1,bs=9999;
    for(int i=0;i<NUM_BUTTONS;i++){
        if(i==idx)continue;
        const Button *b=&BUTTONS[i];
        int dr=b->row-cur->row,dc=b->col-cur->col,off=0;
        switch(dir){
            case 0:if(dr!=-1)continue;off=abs(dc);break;
            case 1:if(dr!= 1)continue;off=abs(dc);break;
            case 2:if(dc>=0||dr!=0)continue;off=abs(dc);break;
            case 3:if(dc<=0||dr!=0)continue;off=abs(dc);break;
        }
        if(off<bs){bs=off;best=i;}
    }
    return best>=0?best:idx;
}

int main(int argc,char *argv[]){
    (void)argc;(void)argv;
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK)<0){
        fprintf(stderr,"SDL_Init: %s\n",SDL_GetError());return 1;
    }
    if(TTF_Init()<0){fprintf(stderr,"TTF_Init: %s\n",TTF_GetError());return 1;}

    /* Pantalla completa en la resolución nativa del dispositivo */
    SDL_Window *win=SDL_CreateWindow("Calculadora",
        SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
        SCREEN_W,SCREEN_H,
        SDL_WINDOW_SHOWN|SDL_WINDOW_FULLSCREEN_DESKTOP);
    if(!win){fprintf(stderr,"Window: %s\n",SDL_GetError());return 1;}

    SDL_Renderer *rend=SDL_CreateRenderer(win,-1,
        SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(!rend){fprintf(stderr,"Renderer: %s\n",SDL_GetError());return 1;}
    /* Forzar render lógico a 1280x720 independiente de la resolución real */
    SDL_RenderSetLogicalSize(rend,SCREEN_W,SCREEN_H);

    /* Buscar fuente TTF */
    const char *fps[]={
        "./fonts/NotoSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/noto/NotoSans-Bold.ttf",
        NULL
    };
    TTF_Font *flg=NULL,*fsm=NULL,*fbtn=NULL,*fhist=NULL;
    for(int i=0;fps[i];i++){
        flg=TTF_OpenFont(fps[i],FONT_LG);
        if(flg){
            fsm  =TTF_OpenFont(fps[i],FONT_SM);
            fbtn =TTF_OpenFont(fps[i],FONT_BTN);
            fhist=TTF_OpenFont(fps[i],FONT_HIST);
            fprintf(stderr,"Font: %s\n",fps[i]);
            break;
        }
    }
    if(!flg){
        fprintf(stderr,"ERROR: No se encontro fuente TTF.\n"
                       "Coloca fonts/NotoSans-Bold.ttf junto al binario.\n");
        SDL_DestroyRenderer(rend);SDL_DestroyWindow(win);
        TTF_Quit();SDL_Quit();return 1;
    }

    SDL_Joystick *joy=NULL;
    if(SDL_NumJoysticks()>0) joy=SDL_JoystickOpen(0);

    CalcState st; memset(&st,0,sizeof(st)); strcpy(st.current,"0");
    int focus=9,running=1;
    Uint32 last_nav=0;

    while(running){
        SDL_Event e;
        while(SDL_PollEvent(&e)){
            if(e.type==SDL_KEYDOWN){
                SDL_Keycode k=e.key.keysym.sym;
                if(k==SDLK_ESCAPE||k==SDLK_q){running=0;continue;}
                if(k>=SDLK_0&&k<=SDLK_9){
                    char v[2]={(char)('0'+(k-SDLK_0)),0};
                    Button t={"","digit",v,BT_NUM,0,0,1};calc_press(&st,&t);
                }
                #define FAKE(act,val) do{Button t={"",act,val,BT_NUM,0,0,1};calc_press(&st,&t);}while(0)
                if(k==SDLK_PERIOD||k==SDLK_KP_PERIOD) FAKE("dot","");
                if(k==SDLK_RETURN||k==SDLK_KP_ENTER)  FAKE("eq","");
                if(k==SDLK_DELETE)                     FAKE("clear","");
                if(k==SDLK_PLUS||k==SDLK_KP_PLUS)     FAKE("op","+");
                if(k==SDLK_MINUS||k==SDLK_KP_MINUS)   FAKE("op","−");
                if(k==SDLK_ASTERISK||k==SDLK_KP_MULTIPLY) FAKE("op","×");
                if(k==SDLK_KP_DIVIDE)                  FAKE("op","÷");
                if(k==SDLK_BACKSPACE){
                    int l=(int)strlen(st.current);
                    if(l>1&&!st.should_reset) st.current[l-1]=0;
                    else strcpy(st.current,"0");
                }
                if(k==SDLK_UP)    focus=nav_move(focus,0);
                if(k==SDLK_DOWN)  focus=nav_move(focus,1);
                if(k==SDLK_LEFT)  focus=nav_move(focus,2);
                if(k==SDLK_RIGHT) focus=nav_move(focus,3);
                if(k==SDLK_SPACE) calc_press(&st,&BUTTONS[focus]);
            }
            /* Joystick / gamepad Trimui
             * Mapeo típico Stock OS:
             *   0=A  1=B  2=X  3=Y
             *   4=L1 5=R1 6=L2 7=R2
             *   8=Select 9=Start
             *  11=D-Up 12=D-Down 13=D-Left 14=D-Right
             */
            if(e.type==SDL_JOYBUTTONDOWN&&joy){
                int b=e.jbutton.button;
                if(b==1) calc_press(&st,&BUTTONS[focus]);
                if(b==0){
                    int l=(int)strlen(st.current);
                    if(l>1&&!st.should_reset) st.current[l-1]=0;
                    else strcpy(st.current,"0");
                }
                if(b==2){Button t={"","clear","",BT_FN,0,0,1};calc_press(&st,&t);}
                if(b==3) running=0;
                if(b==11) focus=nav_move(focus,0);
                if(b==12) focus=nav_move(focus,1);
                if(b==13) focus=nav_move(focus,2);
                if(b==14) focus=nav_move(focus,3);
            }
            if(e.type==SDL_JOYAXISMOTION&&joy){
                Uint32 now=SDL_GetTicks();
                if(now-last_nav>160){last_nav=now;
                    if(e.jaxis.axis==0){
                        if(e.jaxis.value<-16000) focus=nav_move(focus,2);
                        if(e.jaxis.value> 16000) focus=nav_move(focus,3);
                    }
                    if(e.jaxis.axis==1){
                        if(e.jaxis.value<-16000) focus=nav_move(focus,0);
                        if(e.jaxis.value> 16000) focus=nav_move(focus,1);
                    }
                }
            }
            if(e.type==SDL_JOYHATMOTION&&joy){
                Uint32 now=SDL_GetTicks();
                if(now-last_nav>160){last_nav=now;
                    Uint8 h=e.jhat.value;
                    if(h&SDL_HAT_UP)    focus=nav_move(focus,0);
                    if(h&SDL_HAT_DOWN)  focus=nav_move(focus,1);
                    if(h&SDL_HAT_LEFT)  focus=nav_move(focus,2);
                    if(h&SDL_HAT_RIGHT) focus=nav_move(focus,3);
                }
            }
            if(e.type==SDL_QUIT) running=0;
        }

        SDL_Color BG={10,12,16,255};sc(rend,BG);SDL_RenderClear(rend);
        draw_display(rend,flg,fsm,fhist,&st);
        for(int i=0;i<NUM_BUTTONS;i++) draw_btn(rend,fbtn,i,i==focus);
        SDL_RenderPresent(rend);
        SDL_Delay(16);
    }

    TTF_CloseFont(flg);TTF_CloseFont(fsm);
    TTF_CloseFont(fbtn);TTF_CloseFont(fhist);
    if(joy) SDL_JoystickClose(joy);
    SDL_DestroyRenderer(rend);SDL_DestroyWindow(win);
    TTF_Quit();SDL_Quit();
    return 0;
}
