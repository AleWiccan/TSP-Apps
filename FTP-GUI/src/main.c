/*
 * FTP GUI - Trimui Smart Pro
 * ---------------------------
 * Interfaz grafica para controlar el servicio FTP (tcpsvd + busybox ftpd),
 * mostrar la IP actual y cambiar la contrasena del usuario "root"
 * (usado para autenticarse contra el servidor FTP).
 *
 * El servicio FTP se lanza con doble fork + setsid() para que sobreviva
 * a la salida de la app (el Stock OS suele matar el grupo de procesos
 * del launcher al volver al menu).
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

/* ---------------------------------------------------------------------- */
/* Configuracion general                                                  */
/* ---------------------------------------------------------------------- */

#define SCREEN_W 1280
#define SCREEN_H  720

#define FTP_ROOT     "/mnt/SDCARD"
#define FTP_PORT     "21"
#define FTP_MATCH    "tcpsvd -vE 0.0.0.0 21 ftpd" /* patron para pgrep/pkill */
#define FTP_USER     "root"

#define PASS_MAX 64
#define MSG_MAX  128

/* Paleta morada de acento */
static const SDL_Color COL_BG        = {18, 14, 26, 255};   /* fondo casi negro-violeta */
static const SDL_Color COL_PANEL     = {32, 24, 46, 255};   /* panel */
static const SDL_Color COL_ACCENT    = {168, 85, 247, 255}; /* morado principal (#A855F7) */
static const SDL_Color COL_ACCENT_D  = {109, 40, 176, 255}; /* morado oscuro (#6D28B0) */
static const SDL_Color COL_TEXT      = {240, 238, 245, 255};
static const SDL_Color COL_TEXT_DIM  = {160, 152, 175, 255};
static const SDL_Color COL_OK        = {110, 220, 140, 255};
static const SDL_Color COL_ERR       = {235, 90, 100, 255};
static const SDL_Color COL_KEY       = {46, 34, 64, 255};
static const SDL_Color COL_KEY_SEL   = {168, 85, 247, 255};

/* ---------------------------------------------------------------------- */
/* Estado de la aplicacion                                                */
/* ---------------------------------------------------------------------- */

typedef enum {
    STATE_MENU = 0,
    STATE_PASSWORD
} AppState;

typedef struct {
    SDL_Window   *win;
    SDL_Renderer *rend;
    TTF_Font *font_big;
    TTF_Font *font_med;
    TTF_Font *font_small;

    SDL_Joystick *joy;

    int running;
    AppState state;

    /* menu */
    int menu_index;      /* 0 = toggle ftp, 1 = cambiar clave, 2 = salir */
    int ftp_running;
    char ip[64];
    Uint32 last_ip_check;
    Uint32 last_ftp_check;

    /* pantalla de contrasena */
    char pass_buf[PASS_MAX];
    int pass_len;
    int show_pass;
    int kb_row, kb_col;

    /* mensajes temporales (exito / error) */
    char msg[MSG_MAX];
    SDL_Color msg_color;
    Uint32 msg_until;
} App;

/* ---------------------------------------------------------------------- */
/* Teclado virtual                                                        */
/* ---------------------------------------------------------------------- */

/* Cada fila es una lista de "teclas" (cadenas cortas). Las teclas de mas
 * de un caracter son especiales y se identifican por nombre. */
#define KB_ROWS 5
#define KB_MAX_COLS 12

static const char *kb_lower[KB_ROWS][KB_MAX_COLS] = {
    {"1","2","3","4","5","6","7","8","9","0", NULL},
    {"q","w","e","r","t","y","u","i","o","p", NULL},
    {"a","s","d","f","g","h","j","k","l","-","_", NULL},
    {"z","x","c","v","b","n","m",".","@", NULL},
    {"MAYUS","ESPACIO","BORRAR","LIMPIAR","GUARDAR","CANCELAR", NULL}
};

static const char *kb_upper[KB_ROWS][KB_MAX_COLS] = {
    {"1","2","3","4","5","6","7","8","9","0", NULL},
    {"Q","W","E","R","T","Y","U","I","O","P", NULL},
    {"A","S","D","F","G","H","J","K","L","-","_", NULL},
    {"Z","X","C","V","B","N","M",".","@", NULL},
    {"MAYUS","ESPACIO","BORRAR","LIMPIAR","GUARDAR","CANCELAR", NULL}
};

static int kb_shift = 0;

static int kb_row_len(int row) {
    const char **arr = kb_shift ? kb_upper[row] : kb_lower[row];
    int n = 0;
    while (arr[n]) n++;
    return n;
}

/* ---------------------------------------------------------------------- */
/* Utilidades de red / proceso                                            */
/* ---------------------------------------------------------------------- */

/* Busca una IPv4 no-loopback. Prioriza interfaces que contengan "wlan". */
static void get_local_ip(char *out, size_t outlen) {
    struct ifaddrs *ifaddr, *ifa;
    char best[64] = "";
    char fallback[64] = "";

    strncpy(out, "Sin conexion", outlen - 1);
    out[outlen - 1] = '\0';

    if (getifaddrs(&ifaddr) == -1) return;

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        if (strcmp(ifa->ifa_name, "lo") == 0) continue;

        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        char buf[INET_ADDRSTRLEN];
        if (!inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf))) continue;

        if (strstr(ifa->ifa_name, "wlan") != NULL) {
            strncpy(best, buf, sizeof(best) - 1);
        } else if (fallback[0] == '\0') {
            strncpy(fallback, buf, sizeof(fallback) - 1);
        }
    }
    freeifaddrs(ifaddr);

    if (best[0]) {
        strncpy(out, best, outlen - 1);
    } else if (fallback[0]) {
        strncpy(out, fallback, outlen - 1);
    }
    out[outlen - 1] = '\0';
}

/* Comprueba si el proceso del servidor FTP esta corriendo. */
static int ftp_is_running(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "pgrep -f '%s' >/dev/null 2>&1", FTP_MATCH);
    int rc = system(cmd);
    return rc == 0;
}

/* Lanza tcpsvd+ftpd totalmente desacoplado del proceso actual, para que
 * sobreviva a la salida de la app (doble fork + setsid). */
static void ftp_start(void) {
    if (ftp_is_running()) return;

    pid_t pid1 = fork();
    if (pid1 < 0) return;

    if (pid1 == 0) {
        /* Hijo 1: crea nueva sesion y vuelve a hacer fork */
        setsid();

        pid_t pid2 = fork();
        if (pid2 < 0) _exit(1);

        if (pid2 == 0) {
            /* Nieto: proceso final, totalmente desacoplado */
            /* Cierra descriptores estandar */
            int devnull = open("/dev/null", O_RDWR);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                if (devnull > 2) close(devnull);
            }
            if (chdir("/") != 0) { /* ignorar: no critico */ }

            char cmd[256];
            snprintf(cmd, sizeof(cmd), "tcpsvd -vE 0.0.0.0 %s ftpd -w %s",
                     FTP_PORT, FTP_ROOT);
            execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
            _exit(127);
        }
        _exit(0); /* hijo 1 termina, el nieto queda huerfano (adoptado por init) */
    }

    /* Padre: espera a que termine el hijo intermedio (rapido) */
    int status;
    waitpid(pid1, &status, 0);
}

static void ftp_stop(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "pkill -f '%s' >/dev/null 2>&1", FTP_MATCH);
    if (system(cmd) != 0) { /* no hay proceso que matar; no es un error */ }
}

/* Cambia la contrasena del usuario root usando "passwd" (interfaz no
 * interactiva via pipe, igual que hacia el script original). */
static int change_root_password(const char *newpass) {
    FILE *p = popen("passwd " FTP_USER " >/tmp/ftpgui_passwd.log 2>&1", "w");
    if (!p) return 0;
    fprintf(p, "%s\n%s\n", newpass, newpass);
    fflush(p);
    int rc = pclose(p);
    return rc == 0;
}

/* ---------------------------------------------------------------------- */
/* Render helpers                                                         */
/* ---------------------------------------------------------------------- */

static void draw_text(App *a, TTF_Font *font, const char *text, int x, int y,
                       SDL_Color color, int center_x) {
    if (!text || !text[0]) return;
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, color);
    if (!surf) return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(a->rend, surf);
    SDL_Rect dst = { x, y, surf->w, surf->h };
    if (center_x) dst.x = x - surf->w / 2;
    SDL_FreeSurface(surf);
    if (tex) {
        SDL_RenderCopy(a->rend, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
    }
}

static void fill_rounded_ish(App *a, SDL_Rect r, SDL_Color c) {
    SDL_SetRenderDrawColor(a->rend, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(a->rend, &r);
}

static void draw_rect_border(App *a, SDL_Rect r, SDL_Color c, int thickness) {
    SDL_SetRenderDrawColor(a->rend, c.r, c.g, c.b, c.a);
    for (int i = 0; i < thickness; i++) {
        SDL_Rect rr = { r.x - i, r.y - i, r.w + 2 * i, r.h + 2 * i };
        SDL_RenderDrawRect(a->rend, &rr);
    }
}

/* ---------------------------------------------------------------------- */
/* Mensajes temporales                                                    */
/* ---------------------------------------------------------------------- */

static void set_message(App *a, const char *text, SDL_Color color, Uint32 ms) {
    strncpy(a->msg, text, MSG_MAX - 1);
    a->msg[MSG_MAX - 1] = '\0';
    a->msg_color = color;
    a->msg_until = SDL_GetTicks() + ms;
}

/* ---------------------------------------------------------------------- */
/* Pantalla: Menu principal                                               */
/* ---------------------------------------------------------------------- */

#define MENU_ITEMS 3

static void render_menu(App *a) {
    SDL_SetRenderDrawColor(a->rend, COL_BG.r, COL_BG.g, COL_BG.b, 255);
    SDL_RenderClear(a->rend);

    /* Cabecera */
    draw_text(a, a->font_big, "Servidor FTP", 60, 40, COL_TEXT, 0);
    SDL_Rect underline = { 60, 100, 220, 4 };
    fill_rounded_ish(a, underline, COL_ACCENT);

    /* Panel de estado (IP + estado del servicio) */
    SDL_Rect panel = { 60, 150, SCREEN_W - 120, 140 };
    fill_rounded_ish(a, panel, COL_PANEL);
    draw_rect_border(a, panel, COL_ACCENT_D, 2);

    draw_text(a, a->font_small, "Direccion IP", panel.x + 30, panel.y + 20, COL_TEXT_DIM, 0);
    draw_text(a, a->font_med, a->ip, panel.x + 30, panel.y + 46, COL_TEXT, 0);

    const char *status_txt = a->ftp_running ? "ACTIVO" : "DETENIDO";
    SDL_Color status_col = a->ftp_running ? COL_OK : COL_TEXT_DIM;
    draw_text(a, a->font_small, "Estado del servicio", panel.x + 420, panel.y + 20, COL_TEXT_DIM, 0);
    draw_text(a, a->font_med, status_txt, panel.x + 420, panel.y + 46, status_col, 0);

    if (a->ftp_running) {
        char portline[64];
        snprintf(portline, sizeof(portline), "ftp://%s:%s", a->ip, FTP_PORT);
        draw_text(a, a->font_small, portline, panel.x + 30, panel.y + 95, COL_ACCENT, 0);
    } else {
        draw_text(a, a->font_small, "Usuario: root", panel.x + 30, panel.y + 95, COL_TEXT_DIM, 0);
    }

    /* Opciones del menu */
    const char *labels[MENU_ITEMS];
    char toggle_label[64];
    snprintf(toggle_label, sizeof(toggle_label), "%s servicio FTP",
             a->ftp_running ? "Detener" : "Iniciar");
    labels[0] = toggle_label;
    labels[1] = "Cambiar clave";
    labels[2] = "Salir";

    int base_y = 340;
    for (int i = 0; i < MENU_ITEMS; i++) {
        SDL_Rect item = { 60, base_y + i * 90, SCREEN_W - 120, 72 };
        int sel = (i == a->menu_index);
        fill_rounded_ish(a, item, sel ? COL_ACCENT_D : COL_PANEL);
        if (sel) draw_rect_border(a, item, COL_ACCENT, 3);
        draw_text(a, a->font_med, labels[i], item.x + 30, item.y + 18, COL_TEXT, 0);
    }

    /* Mensaje temporal */
    if (a->msg[0] && SDL_GetTicks() < a->msg_until) {
        draw_text(a, a->font_small, a->msg, SCREEN_W / 2, 630, a->msg_color, 1);
    }

    /* Ayuda de botones */
    draw_text(a, a->font_small, "A: Seleccionar    B/SELECT: Salir    D-Pad: Navegar",
               SCREEN_W / 2, 670, COL_TEXT_DIM, 1);

    SDL_RenderPresent(a->rend);
}

/* ---------------------------------------------------------------------- */
/* Pantalla: Cambiar contrasena                                           */
/* ---------------------------------------------------------------------- */

static void render_password(App *a) {
    SDL_SetRenderDrawColor(a->rend, COL_BG.r, COL_BG.g, COL_BG.b, 255);
    SDL_RenderClear(a->rend);

    draw_text(a, a->font_big, "Cambiar clave (root)", 60, 30, COL_TEXT, 0);
    SDL_Rect underline = { 60, 90, 460, 4 };
    fill_rounded_ish(a, underline, COL_ACCENT);

    /* Caja de texto con la contrasena */
    SDL_Rect box = { 60, 120, SCREEN_W - 120, 60 };
    fill_rounded_ish(a, box, COL_PANEL);
    draw_rect_border(a, box, COL_ACCENT_D, 2);

    char display[PASS_MAX + 2];
    if (a->pass_len == 0) {
        strcpy(display, "");
    } else if (a->show_pass) {
        strncpy(display, a->pass_buf, sizeof(display) - 1);
        display[sizeof(display) - 1] = '\0';
    } else {
        int n = a->pass_len < PASS_MAX ? a->pass_len : PASS_MAX;
        for (int i = 0; i < n; i++) display[i] = '*';
        display[n] = '\0';
    }
    draw_text(a, a->font_med, display, box.x + 20, box.y + 14, COL_TEXT, 0);

    char counter[32];
    snprintf(counter, sizeof(counter), "%d/%d", a->pass_len, PASS_MAX - 1);
    draw_text(a, a->font_small, counter, box.x + box.w - 90, box.y + 20, COL_TEXT_DIM, 0);

    draw_text(a, a->font_small, "Y: mostrar/ocultar    minimo 4 caracteres", 60, 195, COL_TEXT_DIM, 0);

    /* Teclado virtual */
    int start_y = 250;
    int row_h = 78;
    for (int r = 0; r < KB_ROWS; r++) {
        int len = kb_row_len(r);
        int key_w = (SCREEN_W - 120) / (r == KB_ROWS - 1 ? len : 10);
        if (key_w > 150) key_w = 150;
        int total_w = key_w * len;
        int start_x = (SCREEN_W - total_w) / 2;

        for (int c = 0; c < len; c++) {
            const char *label = kb_shift ? kb_upper[r][c] : kb_lower[r][c];
            SDL_Rect key = { start_x + c * key_w + 4, start_y + r * row_h, key_w - 8, row_h - 10 };

            int sel = (r == a->kb_row && c == a->kb_col);
            SDL_Color kc = sel ? COL_KEY_SEL : COL_KEY;
            fill_rounded_ish(a, key, kc);
            if (sel) draw_rect_border(a, key, COL_ACCENT, 3);

            SDL_Color txt_col = sel ? COL_BG : COL_TEXT;
            draw_text(a, a->font_small, label, key.x + key.w / 2, key.y + key.h / 2 - 10, txt_col, 1);
        }
    }

    if (a->msg[0] && SDL_GetTicks() < a->msg_until) {
        draw_text(a, a->font_small, a->msg, SCREEN_W / 2, 640, a->msg_color, 1);
    }

    draw_text(a, a->font_small, "A: Elegir    B: Borrar    START: Guardar    SELECT: Cancelar",
               SCREEN_W / 2, 675, COL_TEXT_DIM, 1);

    SDL_RenderPresent(a->rend);
}

/* ---------------------------------------------------------------------- */
/* Logica: teclado virtual                                                */
/* ---------------------------------------------------------------------- */

static void kb_move(App *a, int drow, int dcol) {
    if (drow != 0) {
        a->kb_row = (a->kb_row + drow + KB_ROWS) % KB_ROWS;
        int len = kb_row_len(a->kb_row);
        if (a->kb_col >= len) a->kb_col = len - 1;
    } else {
        int len = kb_row_len(a->kb_row);
        a->kb_col = (a->kb_col + dcol + len) % len;
    }
}

static void pass_append(App *a, const char *s) {
    int add = strlen(s);
    if (a->pass_len + add >= PASS_MAX - 1) return;
    strcpy(a->pass_buf + a->pass_len, s);
    a->pass_len += add;
}

static void pass_backspace(App *a) {
    if (a->pass_len > 0) {
        a->pass_len--;
        a->pass_buf[a->pass_len] = '\0';
    }
}

static void kb_activate(App *a) {
    const char *label = kb_shift ? kb_upper[a->kb_row][a->kb_col] : kb_lower[a->kb_row][a->kb_col];

    if (strcmp(label, "MAYUS") == 0) {
        kb_shift = !kb_shift;
    } else if (strcmp(label, "ESPACIO") == 0) {
        pass_append(a, " ");
    } else if (strcmp(label, "BORRAR") == 0) {
        pass_backspace(a);
    } else if (strcmp(label, "LIMPIAR") == 0) {
        a->pass_len = 0;
        a->pass_buf[0] = '\0';
    } else if (strcmp(label, "GUARDAR") == 0) {
        if (a->pass_len < 4) {
            set_message(a, "La clave debe tener al menos 4 caracteres", COL_ERR, 2500);
            return;
        }
        if (change_root_password(a->pass_buf)) {
            set_message(a, "Clave actualizada correctamente", COL_OK, 2500);
            a->pass_len = 0;
            a->pass_buf[0] = '\0';
            a->state = STATE_MENU;
        } else {
            set_message(a, "Error al cambiar la clave", COL_ERR, 2500);
        }
    } else if (strcmp(label, "CANCELAR") == 0) {
        a->pass_len = 0;
        a->pass_buf[0] = '\0';
        a->state = STATE_MENU;
    } else {
        pass_append(a, label);
    }
}

/* ---------------------------------------------------------------------- */
/* Entrada: botones logicos                                               */
/* ---------------------------------------------------------------------- */

typedef enum {
    BTN_NONE = 0,
    BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT,
    BTN_A, BTN_B, BTN_X, BTN_Y,
    BTN_START, BTN_SELECT
} LogicalButton;

static void handle_button(App *a, LogicalButton btn) {
    if (a->state == STATE_MENU) {
        switch (btn) {
            case BTN_UP:
                a->menu_index = (a->menu_index - 1 + MENU_ITEMS) % MENU_ITEMS;
                break;
            case BTN_DOWN:
                a->menu_index = (a->menu_index + 1) % MENU_ITEMS;
                break;
            case BTN_A:
                if (a->menu_index == 0) {
                    if (a->ftp_running) {
                        ftp_stop();
                        set_message(a, "Servicio FTP detenido", COL_TEXT, 1800);
                    } else {
                        ftp_start();
                        set_message(a, "Servicio FTP iniciado", COL_OK, 1800);
                    }
                    a->ftp_running = ftp_is_running();
                } else if (a->menu_index == 1) {
                    a->state = STATE_PASSWORD;
                    a->kb_row = 0;
                    a->kb_col = 0;
                } else if (a->menu_index == 2) {
                    a->running = 0;
                }
                break;
            case BTN_B:
            case BTN_SELECT:
                a->running = 0;
                break;
            default:
                break;
        }
    } else if (a->state == STATE_PASSWORD) {
        switch (btn) {
            case BTN_UP:    kb_move(a, -1, 0); break;
            case BTN_DOWN:  kb_move(a, 1, 0);  break;
            case BTN_LEFT:  kb_move(a, 0, -1); break;
            case BTN_RIGHT: kb_move(a, 0, 1);  break;
            case BTN_A:     kb_activate(a);    break;
            case BTN_B:     pass_backspace(a); break;
            case BTN_Y:     a->show_pass = !a->show_pass; break;
            case BTN_START: {
                if (a->pass_len < 4) {
                    set_message(a, "La clave debe tener al menos 4 caracteres", COL_ERR, 2500);
                } else if (change_root_password(a->pass_buf)) {
                    set_message(a, "Clave actualizada correctamente", COL_OK, 2500);
                    a->pass_len = 0;
                    a->pass_buf[0] = '\0';
                    a->state = STATE_MENU;
                } else {
                    set_message(a, "Error al cambiar la clave", COL_ERR, 2500);
                }
                break;
            }
            case BTN_SELECT:
                a->pass_len = 0;
                a->pass_buf[0] = '\0';
                a->state = STATE_MENU;
                break;
            default:
                break;
        }
    }
}

/* Traduce el indice de boton SDL_JOYBUTTONDOWN segun el mapeo de Stock OS */
static LogicalButton joybutton_to_logical(int idx) {
    switch (idx) {
        case 0: return BTN_B;
        case 1: return BTN_A;
        case 2: return BTN_Y;
        case 3: return BTN_X;
        case 8: return BTN_SELECT;
        case 9: return BTN_START;
        case 11: return BTN_UP;
        case 12: return BTN_DOWN;
        case 13: return BTN_LEFT;
        case 14: return BTN_RIGHT;
        default: return BTN_NONE;
    }
}

static LogicalButton keysym_to_logical(SDL_Keycode k) {
    switch (k) {
        case SDLK_UP: return BTN_UP;
        case SDLK_DOWN: return BTN_DOWN;
        case SDLK_LEFT: return BTN_LEFT;
        case SDLK_RIGHT: return BTN_RIGHT;
        case SDLK_RETURN: return BTN_A;
        case SDLK_BACKSPACE: return BTN_B;
        case SDLK_ESCAPE: return BTN_SELECT;
        case SDLK_TAB: return BTN_Y;
        case SDLK_p: return BTN_START;
        default: return BTN_NONE;
    }
}

/* ---------------------------------------------------------------------- */
/* main                                                                    */
/* ---------------------------------------------------------------------- */

static TTF_Font *load_font(int size) {
    const char *candidates[] = {
        "./fonts/font.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        NULL
    };
    for (int i = 0; candidates[i]; i++) {
        TTF_Font *f = TTF_OpenFont(candidates[i], size);
        if (f) return f;
    }
    return NULL;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init error: %s\n", TTF_GetError());
        SDL_Quit();
        return 1;
    }

    App a;
    memset(&a, 0, sizeof(a));
    a.running = 1;
    a.state = STATE_MENU;
    a.show_pass = 0;
    strcpy(a.ip, "...");

    a.win = SDL_CreateWindow("FTP GUI",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!a.win) {
        fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        return 1;
    }

    a.rend = SDL_CreateRenderer(a.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!a.rend) {
        a.rend = SDL_CreateRenderer(a.win, -1, SDL_RENDERER_SOFTWARE);
    }
    SDL_RenderSetLogicalSize(a.rend, SCREEN_W, SCREEN_H);

    a.font_big   = load_font(48);
    a.font_med   = load_font(32);
    a.font_small = load_font(22);
    if (!a.font_big || !a.font_med || !a.font_small) {
        fprintf(stderr, "No se pudo cargar ninguna fuente TTF\n");
        return 1;
    }

    if (SDL_NumJoysticks() > 0) {
        a.joy = SDL_JoystickOpen(0);
    }

    a.ip[0] = '\0';
    get_local_ip(a.ip, sizeof(a.ip));
    a.ftp_running = ftp_is_running();
    a.last_ip_check = SDL_GetTicks();
    a.last_ftp_check = SDL_GetTicks();

    SDL_StartTextInput(); /* por si hay teclado fisico/BT conectado */

    while (a.running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT:
                    a.running = 0;
                    break;

                case SDL_JOYBUTTONDOWN: {
                    LogicalButton b = joybutton_to_logical(ev.jbutton.button);
                    if (b != BTN_NONE) handle_button(&a, b);
                    break;
                }

                case SDL_JOYHATMOTION: {
                    if (ev.jhat.value & SDL_HAT_UP)    handle_button(&a, BTN_UP);
                    if (ev.jhat.value & SDL_HAT_DOWN)  handle_button(&a, BTN_DOWN);
                    if (ev.jhat.value & SDL_HAT_LEFT)  handle_button(&a, BTN_LEFT);
                    if (ev.jhat.value & SDL_HAT_RIGHT) handle_button(&a, BTN_RIGHT);
                    break;
                }

                case SDL_JOYAXISMOTION: {
                    const int TH = 16000;
                    if (ev.jaxis.axis == 0) {
                        if (ev.jaxis.value < -TH) handle_button(&a, BTN_LEFT);
                        else if (ev.jaxis.value > TH) handle_button(&a, BTN_RIGHT);
                    } else if (ev.jaxis.axis == 1) {
                        if (ev.jaxis.value < -TH) handle_button(&a, BTN_UP);
                        else if (ev.jaxis.value > TH) handle_button(&a, BTN_DOWN);
                    }
                    break;
                }

                case SDL_KEYDOWN: {
                    LogicalButton b = keysym_to_logical(ev.key.keysym.sym);
                    if (b != BTN_NONE) {
                        handle_button(&a, b);
                    } else if (a.state == STATE_PASSWORD && ev.key.keysym.sym == SDLK_ESCAPE) {
                        a.state = STATE_MENU;
                    }
                    break;
                }

                case SDL_TEXTINPUT: {
                    if (a.state == STATE_PASSWORD) {
                        pass_append(&a, ev.text.text);
                    }
                    break;
                }

                default:
                    break;
            }
        }

        Uint32 now = SDL_GetTicks();
        if (now - a.last_ip_check > 3000) {
            get_local_ip(a.ip, sizeof(a.ip));
            a.last_ip_check = now;
        }
        if (now - a.last_ftp_check > 1500) {
            a.ftp_running = ftp_is_running();
            a.last_ftp_check = now;
        }

        if (a.state == STATE_MENU) render_menu(&a);
        else render_password(&a);

        SDL_Delay(16); /* ~60 fps */
    }

    SDL_StopTextInput();
    if (a.joy) SDL_JoystickClose(a.joy);
    if (a.font_big) TTF_CloseFont(a.font_big);
    if (a.font_med) TTF_CloseFont(a.font_med);
    if (a.font_small) TTF_CloseFont(a.font_small);
    SDL_DestroyRenderer(a.rend);
    SDL_DestroyWindow(a.win);
    TTF_Quit();
    SDL_Quit();

    /* Nota: el servicio FTP (si esta activo) NO se detiene aqui a proposito:
     * fue lanzado desacoplado (setsid + doble fork) para seguir corriendo
     * en segundo plano tras salir de la app. */
    return 0;
}
