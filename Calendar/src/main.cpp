#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <ctime>
#include <cmath>
#include <string>

const int SCREEN_WIDTH  = 1280;
const int SCREEN_HEIGHT = 720;

const int CELL_W = 100;
const int CELL_H = 60;
const int GRID_X  = 140;
const int GRID_Y  = 180;

const char* DAY_NAMES[] = {"Lu", "Ma", "Mi", "Ju", "Vi", "Sa", "Do"};
const char* MONTH_NAMES[] = {"Enero", "Febrero", "Marzo", "Abril",
                             "Mayo", "Junio", "Julio", "Agosto",
                             "Septiembre", "Octubre", "Noviembre", "Diciembre"};

struct CalendarState {
    int year;
    int month; // 0-11
};

// Devuelve el día de la semana del primer día del mes (0=lunes ... 6=domingo)
int firstDayOfWeek(int year, int month) {
    struct tm time_in = {0};
    time_in.tm_year = year - 1900;
    time_in.tm_mon  = month;
    time_in.tm_mday = 1;
    mktime(&time_in);                       // normaliza la fecha
    int wday = time_in.tm_wday;             // 0=domingo…6=sábado
    return (wday == 0) ? 6 : wday - 1;      // convertir a 0=lunes
}

int daysInMonth(int year, int month) {
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int d = days[month];
    if (month == 1) { // febrero
        if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))
            d = 29;
    }
    return d;
}

SDL_Rect getTextSize(const std::string& text, TTF_Font* font) {
    int w = 0, h = 0;
    if (TTF_SizeUTF8(font, text.c_str(), &w, &h) < 0) {
        w = 0; h = 0;
    }
    return {0, 0, w, h};
}

void drawTextCentered(SDL_Renderer* renderer, TTF_Font* font,
                      const std::string& text, SDL_Rect rect, SDL_Color color) {
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surf) return;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_Rect dst;
    dst.w = surf->w;
    dst.h = surf->h;
    dst.x = rect.x + (rect.w - dst.w) / 2;
    dst.y = rect.y + (rect.h - dst.h) / 2;
    SDL_RenderCopy(renderer, texture, nullptr, &dst);
    SDL_FreeSurface(surf);
    SDL_DestroyTexture(texture);
}

void drawFilledRect(SDL_Renderer* renderer, SDL_Rect rect, SDL_Color fill, SDL_Color border) {
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &rect);
}

int main(int argc, char* argv[]) {
    // Inicializar SDL con joystick y video
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
        SDL_Log("Error al inicializar SDL: %s", SDL_GetError());
        return 1;
    }
    if (TTF_Init() < 0) {
        SDL_Log("Error al inicializar SDL_ttf: %s", TTF_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Calendario",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
    if (!window) {
        SDL_Log("Error al crear ventana: %s", SDL_GetError());
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        SDL_Log("Error al crear renderer: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    // Cargar fuente (acepta varias ubicaciones)
    TTF_Font* font = nullptr;
    TTF_Font* bigFont = nullptr;
    const char* fontPaths[] = {
        "font.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        nullptr
    };
    for (int i = 0; fontPaths[i] != nullptr; i++) {
        font = TTF_OpenFont(fontPaths[i], 28);
        if (font) break;
    }
    if (!font) {
        SDL_Log("No se pudo cargar la fuente. Coloca 'font.ttf' en el directorio del ejecutable.");
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }
    bigFont = TTF_OpenFont(fontPaths[0], 72);
    if (!bigFont) bigFont = font; // fallback

    // Abrir joystick si existe
    SDL_Joystick* joystick = nullptr;
    if (SDL_NumJoysticks() > 0) {
        joystick = SDL_JoystickOpen(0);
    }

    // Estado del calendario
    time_t now = time(nullptr);
    struct tm* local = localtime(&now);
    CalendarState cal;
    cal.year  = local->tm_year + 1900;
    cal.month = local->tm_mon;

    // Mapeo de botones (mismo que en el reloj)
    #define BTN_L      4
    #define BTN_R      5
    #define BTN_Y      2
    #define BTN_SELECT 6

    bool running = true;
    SDL_Event event;

    // Colores
    SDL_Color bgColor      = {30, 30, 30, 255};
    SDL_Color gridColor    = {60, 60, 60, 255};
    SDL_Color todayBg      = {0, 120, 215, 255};
    SDL_Color todayText    = {255, 255, 255, 255};
    SDL_Color normalText   = {220, 220, 220, 255};
    SDL_Color headerBg     = {50, 50, 50, 255};
    SDL_Color titleColor   = {255, 255, 255, 255};

    while (running) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_ESCAPE) running = false;
                    if (event.key.keysym.sym == SDLK_LEFT) {
                        if (cal.month == 0) { cal.month = 11; cal.year--; }
                        else cal.month--;
                    }
                    if (event.key.keysym.sym == SDLK_RIGHT) {
                        if (cal.month == 11) { cal.month = 0; cal.year++; }
                        else cal.month++;
                    }
                    if (event.key.keysym.sym == SDLK_UP) cal.year++;
                    if (event.key.keysym.sym == SDLK_DOWN) cal.year--;
                    if (event.key.keysym.sym == SDLK_SPACE) {
                        time_t t = time(nullptr);
                        struct tm* l = localtime(&t);
                        cal.year = l->tm_year + 1900;
                        cal.month = l->tm_mon;
                    }
                    break;
                case SDL_JOYBUTTONDOWN:
                    if (event.jbutton.button == BTN_SELECT) running = false;
                    if (event.jbutton.button == BTN_L) {
                        // mes anterior
                        if (cal.month == 0) { cal.month = 11; cal.year--; }
                        else cal.month--;
                    }
                    if (event.jbutton.button == BTN_R) {
                        // mes siguiente
                        if (cal.month == 11) { cal.month = 0; cal.year++; }
                        else cal.month++;
                    }
                    if (event.jbutton.button == BTN_Y) {
                        // Ir a hoy
                        time_t t = time(nullptr);
                        struct tm* l = localtime(&t);
                        cal.year = l->tm_year + 1900;
                        cal.month = l->tm_mon;
                    }
                    break;
                case SDL_JOYHATMOTION:
                    if (event.jhat.value & SDL_HAT_LEFT) {
                        if (cal.month == 0) { cal.month = 11; cal.year--; }
                        else cal.month--;
                    }
                    if (event.jhat.value & SDL_HAT_RIGHT) {
                        if (cal.month == 11) { cal.month = 0; cal.year++; }
                        else cal.month++;
                    }
                    if (event.jhat.value & SDL_HAT_UP) cal.year++;
                    if (event.jhat.value & SDL_HAT_DOWN) cal.year--;
                    break;
                default:
                    break;
            }
        }

        // Renderizar
        SDL_SetRenderDrawColor(renderer, bgColor.r, bgColor.g, bgColor.b, bgColor.a);
        SDL_RenderClear(renderer);

        // Título: Mes Año
        std::string title = std::string(MONTH_NAMES[cal.month]) + " " + std::to_string(cal.year);
        SDL_Rect titleRect = {0, 40, SCREEN_WIDTH, 80};
        drawTextCentered(renderer, bigFont, title, titleRect, titleColor);

        // Encabezados de los días
        for (int i = 0; i < 7; i++) {
            SDL_Rect cell = {GRID_X + i * CELL_W, GRID_Y, CELL_W, CELL_H};
            drawFilledRect(renderer, cell, headerBg, gridColor);
            drawTextCentered(renderer, font, DAY_NAMES[i], cell, normalText);
        }

        // Datos del mes
        int firstDow = firstDayOfWeek(cal.year, cal.month);
        int totalDays = daysInMonth(cal.year, cal.month);
        int day = 1;
        bool showingCurrent = (cal.year == local->tm_year + 1900) && (cal.month == local->tm_mon);
        int today = local->tm_mday;

        // Dibujar celdas del calendario (máx. 6 semanas)
        for (int row = 0; row < 6; row++) {
            for (int col = 0; col < 7; col++) {
                SDL_Rect cell = {GRID_X + col * CELL_W, GRID_Y + (row + 1) * CELL_H, CELL_W, CELL_H};
                if (row == 0 && col < firstDow) {
                    // Celda vacía al inicio
                    drawFilledRect(renderer, cell, bgColor, gridColor);
                } else if (day <= totalDays) {
                    bool isToday = showingCurrent && (day == today);
                    SDL_Color fill = isToday ? todayBg : bgColor;
                    SDL_Color textCol = isToday ? todayText : normalText;
                    drawFilledRect(renderer, cell, fill, gridColor);
                    drawTextCentered(renderer, font, std::to_string(day), cell, textCol);
                    day++;
                } else {
                    // Ya no hay más días
                    drawFilledRect(renderer, cell, bgColor, gridColor);
                }
            }
        }

        // Instrucciones
        drawTextCentered(renderer, font,
                         "L1: Mes anterior  R1: Mes siguiente  Y: Hoy  SELECT: Salir",
                         {0, SCREEN_HEIGHT - 40, SCREEN_WIDTH, 40}, {150,150,150,255});

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    // Limpiar recursos
    if (bigFont != font) TTF_CloseFont(bigFont);
    TTF_CloseFont(font);
    if (joystick) SDL_JoystickClose(joystick);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}