#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <cmath>

using namespace std;

// ---------- Constantes ----------
#define DEFAULT_PORT 8888
#define CONFIG_FILE  "server.cfg"
#define FONT_PATH    "font.ttf"   // Ruta relativa al ejecutable

// ---------- Estructura del paquete ----------
#pragma pack(push, 1)
struct GamepadState {
    uint16_t buttons;      // máscara de botones
    int16_t leftX;
    int16_t leftY;
    int16_t rightX;
    int16_t rightY;
	int16_t leftTrigger;
    int16_t rightTrigger;
};
#pragma pack(pop)

// Mapeo de botones
#define BTN_A       (1 << 0)
#define BTN_B       (1 << 1)
#define BTN_X       (1 << 2)
#define BTN_Y       (1 << 3)
#define BTN_LB      (1 << 4)
#define BTN_RB      (1 << 5)
#define BTN_BACK    (1 << 6)
#define BTN_START   (1 << 7)
#define BTN_GUIDE   (1 << 8)
#define BTN_LSTICK  (1 << 9)
#define BTN_RSTICK  (1 << 10)
#define BTN_DPAD_U  (1 << 11)
#define BTN_DPAD_D  (1 << 12)
#define BTN_DPAD_L  (1 << 13)
#define BTN_DPAD_R  (1 << 14)

// ---------- Estados de la aplicación ----------
enum AppState {
    STATE_CONFIG,
    STATE_CONNECTING,
    STATE_CONNECTED,
    STATE_QUIT
};

// ---------- Variables globales ----------
SDL_Window   *window   = nullptr;
SDL_Renderer *renderer = nullptr;
TTF_Font     *font     = nullptr;
// ✅ Resolución corregida a 1280x720
const int SCREEN_W = 1280;
const int SCREEN_H = 720;

// Gamepad
SDL_GameController *controller = nullptr;
GamepadState current_state;

// Red
int sock = -1;
sockaddr_in server_addr;
bool server_addr_valid = false;
string server_ip = "192.168.1.100";
int server_port = DEFAULT_PORT;

// Interfaz de configuración
int cursor_pos = 0;       // índice del carácter que se edita (0..14, puntos fijos no editables)
char ip_buffer[16] = "192.168.001.100"; // siempre 15 caracteres + null

// Cargar IP desde archivo
void LoadConfig() {
    FILE *f = fopen(CONFIG_FILE, "r");
    if (f) {
        char ip[32];
        if (fgets(ip, sizeof(ip), f)) {
            ip[strcspn(ip, "\r\n")] = 0;
            int a, b, c, d;                          // ← variables locales
            if (sscanf(ip, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
                server_ip = ip;
                snprintf(ip_buffer, sizeof(ip_buffer), "%03d.%03d.%03d.%03d", a, b, c, d);
            }
        }
        fclose(f);
    }
}

// Guardar IP en archivo
void SaveConfig() {
    FILE *f = fopen(CONFIG_FILE, "w");
    if (f) {
        fprintf(f, "%s\n", server_ip.c_str());
        fclose(f);
    }
}

// Inicializar SDL y gamepad
bool InitSDL() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
        return false;
    }

    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");

    window = SDL_CreateWindow("Gamepad UDP", 
                              SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              SCREEN_W, SCREEN_H, 
                              SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS);
    if (!window) {
        fprintf(stderr, "Window: %s\n", SDL_GetError());
        return false;
    }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        fprintf(stderr, "Renderer: %s\n", SDL_GetError());
        return false;
    }

    font = TTF_OpenFont(FONT_PATH, 36); // ✅ Tamaño de fuente aumentado para mejor legibilidad en 720p
    if (!font) {
        fprintf(stderr, "Font: %s\n", TTF_GetError());
        return false;
    }

    // Abrir el primer mando
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            controller = SDL_GameControllerOpen(i);
            if (controller) break;
        }
    }
    if (!controller) {
        fprintf(stderr, "No gamepad found\n");
        return false;
    }

    return true;
}

// Dibujar texto centrado en x, y
void DrawText(const char *text, int x, int y, SDL_Color color, bool centered = true) {
    SDL_Surface *surf = TTF_RenderText_Solid(font, text, color);
    if (!surf) return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_Rect dst;
    dst.w = surf->w;
    dst.h = surf->h;
    if (centered) {
        dst.x = x - surf->w / 2;
        dst.y = y - surf->h / 2;
    } else {
        dst.x = x;
        dst.y = y;
    }
    SDL_RenderCopy(renderer, tex, NULL, &dst);
    SDL_FreeSurface(surf);
    SDL_DestroyTexture(tex);
}

// ---------- Dibujar pantalla de configuración ----------
void DrawConfigScreen() {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    SDL_Color white = {255,255,255,255};
    SDL_Color yellow = {255,255,0,255};
    
    // ✅ Títulos e instrucciones reposicionados para 1280x720
    DrawText("CONFIGURAR SERVIDOR", SCREEN_W/2, 80, white);
    DrawText("Editar IP con D-Pad, Confirmar=A, Cancelar=B", SCREEN_W/2, 150, white);

    // ✅ Dibujar el buffer con el cursor, ahora más grande y centrado
    char display[32];
    snprintf(display, sizeof(display), "%s", ip_buffer);
    int char_width = 28; // Aproximado para fuente 36
    int start_x = SCREEN_W/2 - (15 * char_width) / 2;
    for (int i = 0; i < 15; i++) {
        char c[2] = {display[i], 0};
        SDL_Color color = (i == cursor_pos) ? yellow : white;
        DrawText(c, start_x + i * char_width + char_width/2, 250, color);
    }
    DrawText("IP:", start_x - 60, 250, white, false);
    
    // Mostrar IP actual guardada
    string saved_msg = "IP guardada: " + server_ip;
    DrawText(saved_msg.c_str(), SCREEN_W/2, 350, white);
}

// ---------- Dibujar pantalla de mando ----------
void DrawControllerScreen() {
    SDL_SetRenderDrawColor(renderer, 20, 20, 40, 255);
    SDL_RenderClear(renderer);

    SDL_Color green = {0,255,0,255};
    SDL_Color white = {255,255,255,255};
    
    // ✅ Información de conexión reposicionada
    DrawText(("Conectado a " + server_ip + ":" + to_string(server_port)).c_str(), SCREEN_W/2, 30, green);

    // ✅ Representación del mando escalada y reposicionada
    // Las coordenadas se han ajustado para una distribución equilibrada en 1280x720
    
    // Cruceta (D-Pad) - Ahora en la esquina superior izquierda
    int dpad_center_x = 150;
    int dpad_center_y = 250;
    SDL_Rect d_up    = {dpad_center_x - 25, dpad_center_y - 70, 50, 50};
    SDL_Rect d_down  = {dpad_center_x - 25, dpad_center_y + 20, 50, 50};
    SDL_Rect d_left  = {dpad_center_x - 70, dpad_center_y - 25, 50, 50};
    SDL_Rect d_right = {dpad_center_x + 20, dpad_center_y - 25, 50, 50};
    
    SDL_SetRenderDrawColor(renderer, (current_state.buttons & BTN_DPAD_U) ? 0 : 100, 255, 0, 255);
    SDL_RenderFillRect(renderer, &d_up);
    SDL_SetRenderDrawColor(renderer, (current_state.buttons & BTN_DPAD_D) ? 0 : 100, 255, 0, 255);
    SDL_RenderFillRect(renderer, &d_down);
    SDL_SetRenderDrawColor(renderer, (current_state.buttons & BTN_DPAD_L) ? 0 : 100, 255, 0, 255);
    SDL_RenderFillRect(renderer, &d_left);
    SDL_SetRenderDrawColor(renderer, (current_state.buttons & BTN_DPAD_R) ? 0 : 100, 255, 0, 255);
    SDL_RenderFillRect(renderer, &d_right);
    
    // Etiquetas para la cruceta
    DrawText("D-Pad", dpad_center_x, dpad_center_y - 120, white);
    
    // Botones A/B/X/Y - Ahora en la esquina superior derecha
    int btn_center_x = 1130;
    int btn_center_y = 250;
    SDL_Rect btn_a = {btn_center_x - 35, btn_center_y - 35, 70, 70};
    SDL_Rect btn_b = {btn_center_x + 35, btn_center_y - 105, 70, 70};
    SDL_Rect btn_x = {btn_center_x - 105, btn_center_y - 105, 70, 70};
    SDL_Rect btn_y = {btn_center_x - 35, btn_center_y - 175, 70, 70};
    
    auto drawBtn = [&](SDL_Rect r, bool pressed) {
        SDL_SetRenderDrawColor(renderer, pressed ? 0 : 150, pressed ? 255 : 150, 0, 255);
        SDL_RenderFillRect(renderer, &r);
    };
    drawBtn(btn_a, current_state.buttons & BTN_A);
    drawBtn(btn_b, current_state.buttons & BTN_B);
    drawBtn(btn_x, current_state.buttons & BTN_X);
    drawBtn(btn_y, current_state.buttons & BTN_Y);
    
    // Etiquetas para botones
    DrawText("B", btn_a.x + 35, btn_a.y + 35, white);
    DrawText("A", btn_b.x + 35, btn_b.y + 35, white);
    DrawText("Y", btn_x.x + 35, btn_x.y + 35, white);
    DrawText("X", btn_y.x + 35, btn_y.y + 35, white);
    
    // Sticks analógicos - Ahora en la parte inferior, más separados
    auto drawStick = [&](int cx, int cy, int16_t x, int16_t y, const char* label) {
        // Círculo exterior
        for (int w = 0; w < 360; w++) {
            SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
            SDL_RenderDrawPoint(renderer, cx + (int)(60 * cos(w * M_PI/180)),
                                cy + (int)(60 * sin(w * M_PI/180)));
        }
        // Punto móvil
        int dx = (x / 32767.0) * 45;
        int dy = (y / 32767.0) * 45;
        SDL_SetRenderDrawColor(renderer, 0, 255, 255, 255);
        SDL_RenderDrawLine(renderer, cx, cy, cx + dx, cy + dy);
        SDL_Rect dot = {cx + dx - 8, cy + dy - 8, 16, 16};
        SDL_RenderFillRect(renderer, &dot);
        
        // Etiqueta del stick
        DrawText(label, cx, cy - 100, white);
    };
    
    drawStick(350, 550, current_state.leftX, current_state.leftY, "Joystick Izquierdo");
    drawStick(930, 550, current_state.rightX, current_state.rightY, "Joystick Derecho");
    
    // Botones adicionales (L1, R1, Start, Select)
    SDL_Color lb_color = (current_state.buttons & BTN_LB) ? green : white;
    DrawText("L1", 50, 600, lb_color, false);
    
    SDL_Color rb_color = (current_state.buttons & BTN_RB) ? green : white;
    DrawText("R1", 1200, 600, rb_color, false);
    
    SDL_Color start_color = (current_state.buttons & BTN_START) ? green : white;
    DrawText("START", SCREEN_W/2, 670, start_color);
    
    SDL_Color back_color = (current_state.buttons & BTN_BACK) ? green : white;
    DrawText("SELECT", SCREEN_W/2 - 200, 670, back_color);
    
    // Instrucción para volver a configuración
    DrawText("START+SELECT para volver a configuracion", SCREEN_W/2, 700, white);
}

// Procesar entrada de configuración
void HandleConfigInput(SDL_Event &event, AppState &state) {
    if (event.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_B: // Confirmar
                {
                    int a,b,c,d;
                    sscanf(ip_buffer, "%d.%d.%d.%d", &a, &b, &c, &d);
                    server_ip = to_string(a) + "." + to_string(b) + "." + to_string(c) + "." + to_string(d);
                    SaveConfig();
                    state = STATE_CONNECTING;
                }
                break;
            case SDL_CONTROLLER_BUTTON_A: // Salir
                state = STATE_QUIT;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                if (cursor_pos >= 0 && cursor_pos < 15 && ip_buffer[cursor_pos] != '.') {
                    if (ip_buffer[cursor_pos] < '9') ip_buffer[cursor_pos]++;
                    else ip_buffer[cursor_pos] = '0';
                }
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                if (cursor_pos >= 0 && cursor_pos < 15 && ip_buffer[cursor_pos] != '.') {
                    if (ip_buffer[cursor_pos] > '0') ip_buffer[cursor_pos]--;
                    else ip_buffer[cursor_pos] = '9';
                }
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                do {
                    cursor_pos--;
                    if (cursor_pos < 0) cursor_pos = 14;
                } while (ip_buffer[cursor_pos] == '.');
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                do {
                    cursor_pos++;
                    if (cursor_pos > 14) cursor_pos = 0;
                } while (ip_buffer[cursor_pos] == '.');
                break;
            default: break;
        }
    }
}

// Intentar conectar por UDP
bool ConnectToServer() {
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return false;
    fcntl(sock, F_SETFL, O_NONBLOCK);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_aton(server_ip.c_str(), &server_addr.sin_addr) == 0) {
        close(sock);
        return false;
    }
    server_addr_valid = true;
    return true;
}

// Actualizar estado del gamepad desde los eventos
void ProcessGamepadEvent(SDL_Event &event) {
    if (event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP) {
        Uint8 sdl_btn = event.cbutton.button;
        bool pressed = event.cbutton.state == SDL_PRESSED;
        uint16_t mask = 0;
        switch (sdl_btn) {
            case SDL_CONTROLLER_BUTTON_A: mask = BTN_A; break;
            case SDL_CONTROLLER_BUTTON_B: mask = BTN_B; break;
            case SDL_CONTROLLER_BUTTON_X: mask = BTN_X; break;
            case SDL_CONTROLLER_BUTTON_Y: mask = BTN_Y; break;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: mask = BTN_LB; break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: mask = BTN_RB; break;
            case SDL_CONTROLLER_BUTTON_BACK: mask = BTN_BACK; break;
            case SDL_CONTROLLER_BUTTON_START: mask = BTN_START; break;
            case SDL_CONTROLLER_BUTTON_GUIDE: mask = BTN_GUIDE; break;
            case SDL_CONTROLLER_BUTTON_LEFTSTICK: mask = BTN_LSTICK; break;
            case SDL_CONTROLLER_BUTTON_RIGHTSTICK: mask = BTN_RSTICK; break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP: mask = BTN_DPAD_U; break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN: mask = BTN_DPAD_D; break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT: mask = BTN_DPAD_L; break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: mask = BTN_DPAD_R; break;
            default: break;
        }
        if (pressed) current_state.buttons |= mask;
        else         current_state.buttons &= ~mask;
    }
    if (event.type == SDL_CONTROLLERAXISMOTION) {
        switch (event.caxis.axis) {
            case SDL_CONTROLLER_AXIS_LEFTX:  current_state.leftX = event.caxis.value; break;
            case SDL_CONTROLLER_AXIS_LEFTY:  current_state.leftY = event.caxis.value; break;
            case SDL_CONTROLLER_AXIS_RIGHTX: current_state.rightX = event.caxis.value; break;
            case SDL_CONTROLLER_AXIS_RIGHTY: current_state.rightY = event.caxis.value; break;
			case SDL_CONTROLLER_AXIS_TRIGGERLEFT: current_state.leftTrigger = event.caxis.value; break;
			case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: current_state.rightTrigger = event.caxis.value; break;
        }
    }
}

int main(int argc, char *argv[]) {
    LoadConfig();
    if (!InitSDL()) return 1;

    AppState state = STATE_CONFIG;
    bool running = true;
    Uint32 lastSend = 0;
    const Uint32 SEND_INTERVAL = 10;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            switch (state) {
                case STATE_CONFIG:
                    HandleConfigInput(event, state);
                    break;
                case STATE_CONNECTING:
                case STATE_CONNECTED:
                    ProcessGamepadEvent(event);
                    break;
                default: break;
            }
        }

        if (state == STATE_CONNECTING) {
            if (ConnectToServer()) {
                state = STATE_CONNECTED;
            } else {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", "No se pudo conectar al servidor.", window);
                state = STATE_CONFIG;
            }
        }

        if (state == STATE_CONNECTED) {
            Uint32 now = SDL_GetTicks();
            if (now - lastSend >= SEND_INTERVAL && server_addr_valid) {
                sendto(sock, &current_state, sizeof(current_state), 0,
                       (sockaddr*)&server_addr, sizeof(server_addr));
                lastSend = now;
            }
            if ((current_state.buttons & BTN_START) && (current_state.buttons & BTN_BACK)) {
                state = STATE_CONFIG;
                if (sock >= 0) { close(sock); sock = -1; }
                server_addr_valid = false;
                SDL_Delay(500);
            }
        }

        // Renderizado según estado
        if (state == STATE_CONFIG) {
            DrawConfigScreen();
        } else if (state == STATE_CONNECTED) {
            DrawControllerScreen();
        } else {
            SDL_SetRenderDrawColor(renderer, 0,0,0,255);
            SDL_RenderClear(renderer);
        }
        SDL_RenderPresent(renderer);

        SDL_Delay(1);
        if (state == STATE_QUIT) running = false;
    }

    // Limpieza
    if (controller) SDL_GameControllerClose(controller);
    if (sock >= 0) close(sock);
    if (font) TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}