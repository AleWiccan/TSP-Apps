/*
 * TrimuiXboxPad - main.c
 * ----------------------
 * Convierte la Trimui Smart Pro en un gamepad Bluetooth HID generico.
 *
 * Botones: se leen como SDL_Joystick "en bruto" (indices fisicos medidos en
 * la consola) y se traducen con una tabla editable (buttons.ini). El mapeo
 * de SDL_GameController de la consola NO es fiable para los botones
 * (A/B cruzados, Y/L1/Select/Start desplazados), asi que solo se usa para
 * los ejes y el D-Pad, que si funcionaban bien.
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <unistd.h>

#include "bt_hid.h"
#include "hid_descriptor.h"

#define SCREEN_W 1280
#define SCREEN_H 720
#define BT_DEVICE_NAME "Trimui Gamepad"
#define LOG(...) do { fprintf(stderr, "[app] " __VA_ARGS__); fputc('\n', stderr); fflush(stderr); } while (0)

typedef struct {
    int16_t lx, ly, rx, ry;   /* crudo de SDL, -32768..32767 */
    uint8_t lt, rt;           /* 0..255 */
    int up, down, left, right;
    uint16_t buttons;         /* bits GBTN_* */
} PadState;

static SDL_Window   *window   = NULL;
static SDL_Renderer *renderer = NULL;
static TTF_Font     *fontBig = NULL, *font = NULL, *fontSmall = NULL;
static SDL_GameController *controller = NULL;
static SDL_Joystick *joy = NULL;
static PadState pad;

/* ------------------------------------------------------------------ */
/* Mapa de botones fisicos (indice SDL_Joystick) -> bit del reporte HID */
/* ------------------------------------------------------------------ */

#define MAX_RAW 32
static int raw_map[MAX_RAW];
static const struct { const char *name; int bit; } btn_names[] = {
    {"A", GBTN_A}, {"B", GBTN_B}, {"X", GBTN_X}, {"Y", GBTN_Y},
    {"LB", GBTN_LB}, {"RB", GBTN_RB}, {"BACK", GBTN_BACK}, {"START", GBTN_START},
    {"GUIDE", GBTN_GUIDE}, {"LSTICK", GBTN_LSTICK}, {"RSTICK", GBTN_RSTICK}
};
#define N_BTN_NAMES ((int)(sizeof(btn_names)/sizeof(btn_names[0])))

static int NameToBit(const char *n)
{
    for (int i = 0; i < N_BTN_NAMES; i++)
        if (strcasecmp(n, btn_names[i].name) == 0) return btn_names[i].bit;
    return -1; /* NONE u otro nombre: sin funcion */
}

static const char *BitName(int bit)
{
    for (int i = 0; i < N_BTN_NAMES; i++)
        if (btn_names[i].bit == bit) return btn_names[i].name;
    return "?";
}

static void LoadButtonMap(void)
{
    for (int i = 0; i < MAX_RAW; i++) raw_map[i] = -1;
    /* Indices medidos en la consola (JEB+): 0=B 1=A 2=X 3=Y 4=L1 5=R1 6=Select 7=Start 8=Menu */
    raw_map[0] = GBTN_B;    raw_map[1] = GBTN_A;
    raw_map[2] = GBTN_X;    raw_map[3] = GBTN_Y;
    raw_map[4] = GBTN_LB;   raw_map[5] = GBTN_RB;
    raw_map[6] = GBTN_BACK; raw_map[7] = GBTN_START;
    raw_map[8] = GBTN_GUIDE;

    FILE *f = fopen("buttons.ini", "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            int idx; char name[32];
            if (line[0] == '#' || line[0] == ';') continue;
            if (sscanf(line, " b%d = %31s", &idx, name) == 2 && idx >= 0 && idx < MAX_RAW) {
                raw_map[idx] = NameToBit(name);
                LOG("buttons.ini: b%d = %s", idx, name);
            }
        }
        fclose(f);
    } else {
        LOG("sin buttons.ini, uso el mapa por defecto");
    }
}

static void RawButton(int idx, int pressed)
{
    int bit = (idx >= 0 && idx < MAX_RAW) ? raw_map[idx] : -1;
    if (pressed)
        LOG("boton fisico %d pulsado -> %s", idx, bit >= 0 ? BitName(bit) : "(sin funcion)");
    if (bit < 0) return;
    if (pressed) pad.buttons |= (1 << bit);
    else         pad.buttons &= ~(1 << bit);
}

/* ------------------------------------------------------------------ */
/* Inicializacion                                                      */
/* ------------------------------------------------------------------ */

static int InitSDLAndController(void)
{
    LOG("SDL_Init...");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
        LOG("ERROR SDL_Init: %s", SDL_GetError());
        return 0;
    }
    LOG("SDL video driver: %s", SDL_GetCurrentVideoDriver());
    if (TTF_Init() != 0) { LOG("ERROR TTF_Init: %s", TTF_GetError()); return 0; }

    window = SDL_CreateWindow("TrimuiXboxPad",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!window) { LOG("ERROR ventana: %s", SDL_GetError()); return 0; }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) { LOG("ERROR renderer: %s", SDL_GetError()); return 0; }
    SDL_RenderSetLogicalSize(renderer, SCREEN_W, SCREEN_H);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    const char *fp = getenv("XBOXPAD_FONT");
    if (!fp) fp = "./fonts/font.ttf";
    fontBig   = TTF_OpenFont(fp, 38);
    font      = TTF_OpenFont(fp, 26);
    fontSmall = TTF_OpenFont(fp, 20);
    if (!font) LOG("AVISO: no se pudo abrir %s (%s); sigo sin texto", fp, TTF_GetError());

    LOG("Joysticks detectados: %d", SDL_NumJoysticks());
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        LOG("  [%d] %s (gamecontroller=%d)", i, SDL_JoystickNameForIndex(i), SDL_IsGameController(i));
        if (!controller && SDL_IsGameController(i))
            controller = SDL_GameControllerOpen(i);
    }
    if (!controller && SDL_NumJoysticks() > 0) {
        joy = SDL_JoystickOpen(0);
        LOG("Sin mapeo GameController: uso SDL_Joystick 0 (%s)", joy ? "ok" : "FALLO");
    }
    if (!controller && !joy)
        LOG("AVISO: no hay mando; la UI arranca igualmente para poder depurar Bluetooth");

    LoadButtonMap();
    return 1;
}

/* ------------------------------------------------------------------ */
/* Eventos                                                             */
/* ------------------------------------------------------------------ */

static void HandleEvent(SDL_Event *ev)
{
    switch (ev->type) {
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
        RawButton(ev->jbutton.button, ev->type == SDL_JOYBUTTONDOWN);
        break;

    /* --- con GameController: solo D-Pad y ejes --- */
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP: {
        int p = ev->cbutton.state == SDL_PRESSED;
        switch (ev->cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:    pad.up = p; break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  pad.down = p; break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  pad.left = p; break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: pad.right = p; break;
            default: break;
        }
        break;
    }
    case SDL_CONTROLLERAXISMOTION:
        switch (ev->caxis.axis) {
            case SDL_CONTROLLER_AXIS_LEFTX:  pad.lx = ev->caxis.value; break;
            case SDL_CONTROLLER_AXIS_LEFTY:  pad.ly = ev->caxis.value; break;
            case SDL_CONTROLLER_AXIS_RIGHTX: pad.rx = ev->caxis.value; break;
            case SDL_CONTROLLER_AXIS_RIGHTY: pad.ry = ev->caxis.value; break;
            case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
                pad.lt = (uint8_t)(ev->caxis.value * 255 / 32767); break;
            case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
                pad.rt = (uint8_t)(ev->caxis.value * 255 / 32767); break;
            default: break;
        }
        break;

    /* --- sin GameController: hat y ejes en bruto (medidos en la consola) --- */
    case SDL_JOYHATMOTION:
        if (controller) break;
        pad.up    = (ev->jhat.value & SDL_HAT_UP) != 0;
        pad.down  = (ev->jhat.value & SDL_HAT_DOWN) != 0;
        pad.left  = (ev->jhat.value & SDL_HAT_LEFT) != 0;
        pad.right = (ev->jhat.value & SDL_HAT_RIGHT) != 0;
        break;
    case SDL_JOYAXISMOTION:
        if (controller) break;
        switch (ev->jaxis.axis) {
            case 0: pad.lx = ev->jaxis.value; break;
            case 1: pad.ly = ev->jaxis.value; break;
            case 3: pad.rx = ev->jaxis.value; break;
            case 4: pad.ry = ev->jaxis.value; break;
            case 2: pad.lt = (uint8_t)((ev->jaxis.value + 32768) * 255 / 65535); break;
            case 5: pad.rt = (uint8_t)((ev->jaxis.value + 32768) * 255 / 65535); break;
            default: break;
        }
        break;
    default: break;
    }
}

/* ------------------------------------------------------------------ */
/* Reporte HID                                                         */
/* ------------------------------------------------------------------ */

static int8_t ScaleAxis(int16_t v)
{
    int32_t s = ((int32_t)v * 127) / 32767;
    if (s > 127) s = 127;
    if (s < -127) s = -127;
    return (int8_t)s;
}

static uint8_t HatFromDpad(void)
{
    /* 0=N,1=NE,2=E,3=SE,4=S,5=SW,6=W,7=NW, 8=centrado */
    if (pad.up    && pad.right) return 1;
    if (pad.down  && pad.right) return 3;
    if (pad.down  && pad.left)  return 5;
    if (pad.up    && pad.left)  return 7;
    if (pad.up)    return 0;
    if (pad.right) return 2;
    if (pad.down)  return 4;
    if (pad.left)  return 6;
    return 8;
}

static void BuildReport(uint8_t out[GAMEPAD_REPORT_DATA_LEN])
{
    out[0] = (uint8_t)ScaleAxis(pad.lx);
    out[1] = (uint8_t)ScaleAxis(pad.ly);
    out[2] = (uint8_t)ScaleAxis(pad.rx);
    out[3] = (uint8_t)ScaleAxis(pad.ry);
    out[4] = pad.lt;
    out[5] = pad.rt;
    out[6] = (HatFromDpad() & 0x0F);
    uint16_t btn = pad.buttons & 0x7FFF;
    out[7] = (uint8_t)(btn & 0xFF);
    out[8] = (uint8_t)((btn >> 8) & 0x7F);
}

/* ------------------------------------------------------------------ */
/* Dibujo                                                              */
/* ------------------------------------------------------------------ */

typedef struct { Uint8 r, g, b; } Col;

static void SetCol(Col c, Uint8 a) { SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, a); }

static Col Mix(Col a, Col b, float t)
{
    Col c = { (Uint8)(a.r + (b.r - a.r) * t), (Uint8)(a.g + (b.g - a.g) * t), (Uint8)(a.b + (b.b - a.b) * t) };
    return c;
}

static void FillCircle(int cx, int cy, int r)
{
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)sqrt((double)(r * r - dy * dy));
        SDL_RenderDrawLine(renderer, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

static void FillRoundRect(int x, int y, int w, int h, int r)
{
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    SDL_Rect a = { x + r, y, w - 2 * r, h };
    SDL_Rect b = { x, y + r, w, h - 2 * r };
    SDL_RenderFillRect(renderer, &a);
    SDL_RenderFillRect(renderer, &b);
    FillCircle(x + r, y + r, r);         FillCircle(x + w - r - 1, y + r, r);
    FillCircle(x + r, y + h - r - 1, r); FillCircle(x + w - r - 1, y + h - r - 1, r);
}

/* align: 0 izquierda, 1 centro, 2 derecha */
static void DrawText(TTF_Font *f, const char *text, int x, int y, SDL_Color color, int align)
{
    if (!f) return;
    SDL_Surface *surf = TTF_RenderUTF8_Blended(f, text, color);
    if (!surf) return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_Rect dst = { x, y - surf->h / 2, surf->w, surf->h };
    if (align == 1) dst.x = x - surf->w / 2;
    else if (align == 2) dst.x = x - surf->w;
    SDL_RenderCopy(renderer, tex, NULL, &dst);
    SDL_FreeSurface(surf);
    SDL_DestroyTexture(tex);
}

static void DrawPill(int x, int y, int w, int h, int pressed, Col on, const char *label)
{
    Col off = { 44, 50, 80 };
    SetCol(pressed ? on : off, 255);
    FillRoundRect(x, y, w, h, h / 2);
    SDL_Color t = pressed ? (SDL_Color){10, 12, 24, 255} : (SDL_Color){200, 205, 230, 255};
    DrawText(font, label, x + w / 2, y + h / 2, t, 1);
}

static void DrawStick(int cx, int cy, int16_t vx, int16_t vy, const char *name)
{
    Col ring = { 62, 68, 108 }, well = { 26, 30, 52 }, knob = { 120, 140, 230 };
    float fx = vx / 32767.0f, fy = vy / 32767.0f;
    float mag = sqrtf(fx * fx + fy * fy);
    SetCol(ring, 255); FillCircle(cx, cy, 78);
    SetCol(well, 255); FillCircle(cx, cy, 72);
    SetCol(Mix(knob, (Col){255, 255, 255}, mag > 1 ? 1 : mag * 0.6f), 255);
    FillCircle(cx + (int)(fx * 38), cy + (int)(fy * 38), 32);
    char buf[48]; snprintf(buf, sizeof(buf), "%s  %d, %d", name, ScaleAxis(vx), ScaleAxis(vy));
    DrawText(fontSmall, buf, cx, cy + 108, (SDL_Color){150, 156, 190, 255}, 1);
}

static void DrawDpadArm(int x, int y, int w, int h, int pressed)
{
    Col off = { 62, 68, 108 }, on = { 255, 210, 70 };
    SetCol(pressed ? on : off, 255);
    FillRoundRect(x, y, w, h, 10);
}

static void DrawFace(int cx, int cy, Col c, int pressed, const char *label)
{
    Col base = { 30, 34, 58 };
    SetCol((Col){ 70, 76, 116 }, 255); FillCircle(cx, cy, 40);
    SetCol(pressed ? c : Mix(base, c, 0.22f), 255); FillCircle(cx, cy, 35);
    DrawText(font, label, cx, cy, pressed ? (SDL_Color){10, 12, 24, 255} : (SDL_Color){220, 224, 245, 255}, 1);
}

static void DrawTrigger(int x, int y, int h, uint8_t v, const char *label)
{
    SetCol((Col){ 34, 38, 62 }, 255); FillRoundRect(x, y, 30, h, 12);
    int fh = (int)(v / 255.0f * (h - 6));
    if (fh > 0) { SetCol((Col){ 255, 160, 60 }, 255); FillRoundRect(x + 3, y + h - 3 - fh, 24, fh, 9); }
    DrawText(fontSmall, label, x + 15, y - 20, (SDL_Color){180, 186, 215, 255}, 1);
}

static void DrawUI(bt_hid_ctx_t *bt)
{
    Uint32 now = SDL_GetTicks();

    /* fondo en degradado */
    for (int y = 0; y < SCREEN_H; y++) {
        Col c = Mix((Col){12, 14, 28}, (Col){30, 22, 52}, y / (float)SCREEN_H);
        SetCol(c, 255);
        SDL_RenderDrawLine(renderer, 0, y, SCREEN_W - 1, y);
    }

    /* cabecera */
    DrawText(fontBig, "TRIMUI GAMEPAD", SCREEN_W / 2, 38, (SDL_Color){255, 255, 255, 255}, 1);
    SetCol((Col){ 24, 28, 50 }, 255);
    FillRoundRect(SCREEN_W / 2 - 330, 72, 660, 42, 21);
    char msg[96];
    Col dot; SDL_Color tc = {225, 230, 250, 255};
    if (!bt) {
        dot = (Col){ 235, 70, 70 };
        snprintf(msg, sizeof(msg), "Bluetooth no disponible (revisa log.txt)");
    } else if (bt_hid_is_connected(bt)) {
        dot = (Col){ 70, 220, 120 };
        snprintf(msg, sizeof(msg), "Conectado a %s", bt_hid_peer_address(bt));
    } else {
        float p = 0.55f + 0.45f * (0.5f + 0.5f * sinf(now / 300.0f));
        dot = Mix((Col){ 40, 34, 20 }, (Col){ 255, 200, 40 }, p);
        snprintf(msg, sizeof(msg), "Esperando emparejamiento: busca \"" BT_DEVICE_NAME "\"");
    }
    SetCol(dot, 255); FillCircle(SCREEN_W / 2 - 300, 93, 9);
    DrawText(font, msg, SCREEN_W / 2 - 280, 93, tc, 0);

    /* cuerpo del mando */
    SetCol((Col){ 52, 58, 96 }, 255); FillRoundRect(76, 146, 1128, 508, 46);
    SetCol((Col){ 22, 26, 46 }, 255); FillRoundRect(80, 150, 1120, 500, 42);

    /* hombros y gatillos */
    DrawPill(190, 166, 200, 46, (pad.buttons >> GBTN_LB) & 1, (Col){ 110, 170, 255 }, "LB");
    DrawPill(890, 166, 200, 46, (pad.buttons >> GBTN_RB) & 1, (Col){ 110, 170, 255 }, "RB");
    DrawTrigger(112, 262, 200, pad.lt, "LT");
    DrawTrigger(1138, 262, 200, pad.rt, "RT");

    /* sticks */
    DrawStick(290, 330, pad.lx, pad.ly, "L");
    DrawStick(810, 480, pad.rx, pad.ry, "R");

    /* D-Pad */
    int dx = 470, dy = 480;
    DrawDpadArm(dx - 23, dy - 78, 46, 54, pad.up);
    DrawDpadArm(dx - 23, dy + 24, 46, 54, pad.down);
    DrawDpadArm(dx - 78, dy - 23, 54, 46, pad.left);
    DrawDpadArm(dx + 24, dy - 23, 54, 46, pad.right);
    SetCol((Col){ 40, 44, 74 }, 255); FillRoundRect(dx - 25, dy - 25, 50, 50, 8);

    /* centro: Select / Start */
    DrawPill(490, 310, 140, 42, (pad.buttons >> GBTN_BACK) & 1, (Col){ 210, 214, 235 }, "SELECT");
    DrawPill(650, 310, 140, 42, (pad.buttons >> GBTN_START) & 1, (Col){ 210, 214, 235 }, "START");

    /* A B X Y (colores estilo Xbox) */
    int fx = 990, fy = 350, o = 76;
    DrawFace(fx,     fy + o, (Col){  80, 205, 105 }, (pad.buttons >> GBTN_A) & 1, "A");
    DrawFace(fx + o, fy,     (Col){ 235,  75,  75 }, (pad.buttons >> GBTN_B) & 1, "B");
    DrawFace(fx - o, fy,     (Col){  75, 145, 255 }, (pad.buttons >> GBTN_X) & 1, "X");
    DrawFace(fx,     fy - o, (Col){ 245, 205,  55 }, (pad.buttons >> GBTN_Y) & 1, "Y");

    DrawText(fontSmall, "SELECT + START para salir", SCREEN_W / 2, 690, (SDL_Color){120, 126, 160, 255}, 1);
}

static void SaveSnapshot(const char *path)
{
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_W, SCREEN_H, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return;
    SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
}

/* ------------------------------------------------------------------ */

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    signal(SIGPIPE, SIG_IGN);
    memset(&pad, 0, sizeof(pad));
    LOG("arrancando, uid=%d", (int)getuid());

    if (!InitSDLAndController()) { LOG("Fallo de inicializacion SDL, salgo"); return 1; }

    /* Solo para desarrollo: XBOXPAD_SNAPSHOT=ruta.bmp guarda una captura y sale */
    const char *snap = getenv("XBOXPAD_SNAPSHOT");
    if (snap) {
        pad.buttons = (1 << GBTN_A) | (1 << GBTN_RB) | (1 << GBTN_BACK);
        pad.lx = 20000; pad.ly = -12000; pad.rx = -9000; pad.ry = 26000;
        pad.lt = 140; pad.rt = 40; pad.up = 1; pad.right = 1;
        for (int i = 0; i < 3; i++) { DrawUI(NULL); SDL_RenderPresent(renderer); }
        DrawUI(NULL);
        SaveSnapshot(snap);
        return 0;
    }

    LOG("SDL listo, arrancando Bluetooth HID...");
    bt_hid_ctx_t *bt = bt_hid_start(BT_DEVICE_NAME);
    if (!bt) LOG("ERROR: no se pudo iniciar el servicio Bluetooth HID (ver mensajes [bt_hid] arriba)");

    int running = 1;
    Uint32 last_send = 0;
    const Uint32 SEND_INTERVAL_MS = 10; /* ~100 Hz */

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            HandleEvent(&ev);
        }

        if (bt) bt_hid_poll(bt);

        Uint32 now = SDL_GetTicks();
        if (bt && bt_hid_is_connected(bt) && now - last_send >= SEND_INTERVAL_MS) {
            uint8_t report[GAMEPAD_REPORT_DATA_LEN];
            BuildReport(report);
            bt_hid_send_report(bt, report, sizeof(report));
            last_send = now;
        }

        if ((pad.buttons & (1 << GBTN_BACK)) && (pad.buttons & (1 << GBTN_START)))
            running = 0;

        DrawUI(bt);
        SDL_RenderPresent(renderer);
        SDL_Delay(1);
    }

    LOG("saliendo, cerrando Bluetooth");
    if (bt) bt_hid_stop(bt);
    if (controller) SDL_GameControllerClose(controller);
    if (joy) SDL_JoystickClose(joy);
    if (fontBig) TTF_CloseFont(fontBig);
    if (font) TTF_CloseFont(font);
    if (fontSmall) TTF_CloseFont(fontSmall);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
