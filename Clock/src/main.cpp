#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cmath>
#include <ctime>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cstring>

const int SCREEN_WIDTH  = 1280;
const int SCREEN_HEIGHT = 720;

enum Mode {
    MODE_CLOCK,
    MODE_STOPWATCH,
    MODE_TIMER
};

enum SettingFocus {
    FOCUS_NONE,
    FOCUS_ALARM_HOUR,
    FOCUS_ALARM_MINUTE,
    FOCUS_TIMER_HOURS,
    FOCUS_TIMER_MINUTES,
    FOCUS_TIMER_SECONDS
};

struct Beeper {
    SDL_AudioDeviceID dev = 0;
    bool playing = false;
    float frequency = 600.0f;
    float volume = 0.3f;
    int sampleRate = 22050;
    Uint32 samplesGenerated = 0;

    void start() {
        if (dev == 0) {
            SDL_AudioSpec want, have;
            SDL_zero(want);
            want.freq = sampleRate;
            want.format = AUDIO_S16SYS;
            want.channels = 1;
            want.samples = 2048;
            want.callback = audioCallback;
            want.userdata = this;
            dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
            if (dev == 0) return;
        }
        SDL_PauseAudioDevice(dev, 0);
        playing = true;
    }

    void stop() {
        if (dev != 0) {
            SDL_PauseAudioDevice(dev, 1);
        }
        playing = false;
    }

    void close() {
        if (dev != 0) {
            SDL_CloseAudioDevice(dev);
            dev = 0;
        }
    }

    static void audioCallback(void* userdata, Uint8* stream, int len) {
        Beeper* beep = static_cast<Beeper*>(userdata);
        Sint16* buffer = (Sint16*)stream;
        int samples = len / 2;
        for (int i = 0; i < samples; i++) {
            if (beep->playing) {
                float t = (float)(beep->samplesGenerated + i) / beep->sampleRate;
                float val = std::sin(2.0f * M_PI * beep->frequency * t);
                buffer[i] = (Sint16)(val * 32767.0f * beep->volume);
            } else {
                buffer[i] = 0;
            }
        }
        beep->samplesGenerated += samples;
    }
};

SDL_Window*   window   = nullptr;
SDL_Renderer* renderer = nullptr;
TTF_Font*     font     = nullptr;
TTF_Font*     bigFont  = nullptr;
Mode          currentMode = MODE_CLOCK;
SettingFocus  settingFocus = FOCUS_NONE;

int   alarmHour   = 7;
int   alarmMinute = 0;
bool  alarmEnabled = false;
bool  alarmTriggered = false;
Beeper beeper;

bool     stopwatchRunning = false;
Uint32   stopwatchStartTicks = 0;
Uint32   stopwatchAccumulated = 0;
std::vector<Uint32> laps;

bool     timerRunning = false;
Uint32   timerRemaining = 0;
Uint32   timerSet = 300;
Uint32   timerLastTick = 0;

// Mapeo de botones físicos (corregido tras intercambio X/Y, A/B, SELECT/START)
#define BTN_A      1
#define BTN_B      0
#define BTN_X      3
#define BTN_Y      2
#define BTN_L      4
#define BTN_R      5
#define BTN_START  7
#define BTN_SELECT 6
// Para salir de la aplicación usamos SELECT (el botón Menu)
#define BTN_EXIT BTN_SELECT

SDL_Rect getTextSize(const std::string& text, TTF_Font* f) {
    int w = 0, h = 0;
    if (TTF_SizeUTF8(f, text.c_str(), &w, &h) < 0) {
        w = 0; h = 0;
    }
    return {0, 0, w, h};
}

void drawTextCentered(const std::string& text, int x, int y, int w, int h, TTF_Font* f, SDL_Color color) {
    SDL_Surface* surf = TTF_RenderUTF8_Blended(f, text.c_str(), color);
    if (!surf) return;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_Rect dst;
    dst.w = surf->w;
    dst.h = surf->h;
    dst.x = x + (w - dst.w) / 2;
    dst.y = y + (h - dst.h) / 2;
    SDL_RenderCopy(renderer, texture, nullptr, &dst);
    SDL_FreeSurface(surf);
    SDL_DestroyTexture(texture);
}

void drawFilledRect(SDL_Rect r, SDL_Color fill, SDL_Color border) {
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &r);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &r);
}

std::string format2d(int val) {
    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << val;
    return oss.str();
}

std::string formatTime(int hours, int minutes, int seconds) {
    return format2d(hours) + ":" + format2d(minutes) + ":" + format2d(seconds);
}

std::string formatStopwatch(Uint32 ms) {
    int totalSec = ms / 1000;
    int h = totalSec / 3600;
    int m = (totalSec % 3600) / 60;
    int s = totalSec % 60;
    int cent = (ms % 1000) / 10;
    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << h << ":"
        << std::setw(2) << std::setfill('0') << m << ":"
        << std::setw(2) << std::setfill('0') << s << "."
        << std::setw(2) << std::setfill('0') << cent;
    return oss.str();
}

Uint32 getCurrentMillis() {
    return SDL_GetTicks();
}

void switchMode(Mode m) {
    currentMode = m;
    settingFocus = FOCUS_NONE;
    if (alarmTriggered) {
        beeper.stop();
        alarmTriggered = false;
    }
}

void cycleMode(int delta) {
    int m = static_cast<int>(currentMode);
    m = (m + delta + 3) % 3;
    switchMode(static_cast<Mode>(m));
}

// Manejo global de eventos
void handleGlobalEvents(SDL_Event& e, bool& running) {
    if (e.type == SDL_QUIT) running = false;
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = false;

    if (e.type == SDL_JOYBUTTONDOWN) {
        if (e.jbutton.button == BTN_EXIT) running = false;   // SELECT -> Salir
        if (e.jbutton.button == BTN_L) cycleMode(-1);
        if (e.jbutton.button == BTN_R) cycleMode(1);
    }
    if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_LEFT) cycleMode(-1);
        if (e.key.keysym.sym == SDLK_RIGHT) cycleMode(1);
    }
}

// ----- Reloj -----
void processClockInput(SDL_Event& e) {
    if (alarmTriggered) {
        if (e.type == SDL_JOYBUTTONDOWN || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_SPACE)) {
            beeper.stop();
            alarmTriggered = false;
            alarmEnabled = false;
            return;
        }
    }

    if (e.type == SDL_KEYDOWN) {
        if (settingFocus == FOCUS_ALARM_HOUR) {
            if (e.key.keysym.sym == SDLK_UP) alarmHour = (alarmHour + 1) % 24;
            if (e.key.keysym.sym == SDLK_DOWN) alarmHour = (alarmHour + 23) % 24;
            if (e.key.keysym.sym == SDLK_RIGHT) settingFocus = FOCUS_ALARM_MINUTE;
        } else if (settingFocus == FOCUS_ALARM_MINUTE) {
            if (e.key.keysym.sym == SDLK_UP) alarmMinute = (alarmMinute + 1) % 60;
            if (e.key.keysym.sym == SDLK_DOWN) alarmMinute = (alarmMinute + 59) % 60;
            if (e.key.keysym.sym == SDLK_LEFT) settingFocus = FOCUS_ALARM_HOUR;
        }
        if (e.key.keysym.sym == SDLK_RETURN) {
            settingFocus = (settingFocus == FOCUS_NONE) ? FOCUS_ALARM_HOUR : FOCUS_NONE;
        }
        if (e.key.keysym.sym == SDLK_SPACE) {
            alarmEnabled = !alarmEnabled;
        }
    }

    if (e.type == SDL_JOYBUTTONDOWN) {
        if (e.jbutton.button == BTN_START) alarmEnabled = !alarmEnabled;
        if (e.jbutton.button == BTN_Y) {   // Y: entrar/salir del ajuste de alarma
            settingFocus = (settingFocus == FOCUS_NONE) ? FOCUS_ALARM_HOUR : FOCUS_NONE;
        }
    }
    if (e.type == SDL_JOYHATMOTION && settingFocus != FOCUS_NONE) {
        int val = e.jhat.value;
        if (val & SDL_HAT_UP) {
            if (settingFocus == FOCUS_ALARM_HOUR) alarmHour = (alarmHour + 1) % 24;
            if (settingFocus == FOCUS_ALARM_MINUTE) alarmMinute = (alarmMinute + 1) % 60;
        }
        if (val & SDL_HAT_DOWN) {
            if (settingFocus == FOCUS_ALARM_HOUR) alarmHour = (alarmHour + 23) % 24;
            if (settingFocus == FOCUS_ALARM_MINUTE) alarmMinute = (alarmMinute + 59) % 60;
        }
        if (val & SDL_HAT_RIGHT && settingFocus == FOCUS_ALARM_HOUR) settingFocus = FOCUS_ALARM_MINUTE;
        if (val & SDL_HAT_LEFT && settingFocus == FOCUS_ALARM_MINUTE) settingFocus = FOCUS_ALARM_HOUR;
    }
}

void updateClock() {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    if (alarmEnabled && !alarmTriggered && t->tm_hour == alarmHour && t->tm_min == alarmMinute && t->tm_sec == 0) {
        alarmTriggered = true;
        beeper.start();
    }
}

void renderClock() {
    SDL_Color white = {255,255,255,255};
    SDL_Color gray  = {180,180,180,255};
    SDL_Color yellow = {255,255,0,255};

    time_t now = time(nullptr);
    struct tm* t = localtime(&now);

    std::string timeStr = formatTime(t->tm_hour, t->tm_min, t->tm_sec);
    drawTextCentered(timeStr, 0, 80, SCREEN_WIDTH, 200, bigFont, white);

    char dateBuf[64];
    strftime(dateBuf, sizeof(dateBuf), "%A %d de %B de %Y", t);
    drawTextCentered(dateBuf, 0, 280, SCREEN_WIDTH, 50, font, gray);

    // Texto completo de alarma
    std::string estado = alarmEnabled ? "ON" : "OFF";
    std::string prefix = "Alarma: ";
    std::string hourStr = format2d(alarmHour);
    std::string minStr = format2d(alarmMinute);
    std::string suffix = " (" + estado + ")";
    std::string fullAlarm = prefix + hourStr + ":" + minStr + suffix;

    SDL_Rect alRect = {300, 400, 680, 60};
    SDL_Rect fullSize = getTextSize(fullAlarm, font);
    int startX = alRect.x + (alRect.w - fullSize.w) / 2;
    int startY = alRect.y + (alRect.h - fullSize.h) / 2;

    // Dibujar alarma
    drawTextCentered(fullAlarm, alRect.x, alRect.y, alRect.w, alRect.h, font, white);

    // Resalte semitransparente si estamos ajustando
    if (settingFocus == FOCUS_ALARM_HOUR || settingFocus == FOCUS_ALARM_MINUTE) {
        SDL_Rect prefSize = getTextSize(prefix, font);
        SDL_Rect hSize = getTextSize(hourStr, font);
        SDL_Rect colonSize = getTextSize(":", font);
        int xHour = startX + prefSize.w;
        int xMin = xHour + hSize.w + colonSize.w;

        SDL_Rect highlight;
        highlight.y = startY - 2;
        highlight.h = fullSize.h + 4;
        if (settingFocus == FOCUS_ALARM_HOUR) {
            highlight.x = xHour - 2;
            highlight.w = hSize.w + 4;
        } else {
            SDL_Rect mSize = getTextSize(minStr, font);
            highlight.x = xMin - 2;
            highlight.w = mSize.w + 4;
        }

        // Habilitar blending para transparencia
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 255, 255, 0, 80);
        SDL_RenderFillRect(renderer, &highlight);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    if (alarmTriggered) {
        drawTextCentered("¡ALARMA! (Pulsa cualquier botón)", 300, 480, 680, 60, font, {255,0,0,255});
    }
    drawTextCentered("START: Activar  Y: Ajustar  SELECT: Salir", 0, 600, SCREEN_WIDTH, 40, font, gray);
}

// ----- Cronómetro -----
void processStopwatchInput(SDL_Event& e) {
    if (e.type == SDL_JOYBUTTONDOWN) {
        if (e.jbutton.button == BTN_A) { // A: inicio/pausa
            if (!stopwatchRunning) {
                stopwatchRunning = true;
                stopwatchStartTicks = getCurrentMillis() - stopwatchAccumulated;
            } else {
                stopwatchRunning = false;
                stopwatchAccumulated = getCurrentMillis() - stopwatchStartTicks;
            }
        }
        if (e.jbutton.button == BTN_B) { // B: reiniciar
            stopwatchRunning = false;
            stopwatchAccumulated = 0;
            stopwatchStartTicks = 0;
            laps.clear();
        }
        if (e.jbutton.button == BTN_X) { // X: vuelta
            if (stopwatchRunning || stopwatchAccumulated > 0) {
                Uint32 elapsed = stopwatchRunning ? (getCurrentMillis() - stopwatchStartTicks) : stopwatchAccumulated;
                laps.push_back(elapsed);
            }
        }
    }
    if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_SPACE) {
            if (!stopwatchRunning) {
                stopwatchRunning = true;
                stopwatchStartTicks = getCurrentMillis() - stopwatchAccumulated;
            } else {
                stopwatchRunning = false;
                stopwatchAccumulated = getCurrentMillis() - stopwatchStartTicks;
            }
        }
        if (e.key.keysym.sym == SDLK_r) {
            stopwatchRunning = false;
            stopwatchAccumulated = 0;
            stopwatchStartTicks = 0;
            laps.clear();
        }
        if (e.key.keysym.sym == SDLK_l) {
            if (stopwatchRunning || stopwatchAccumulated > 0) {
                Uint32 elapsed = stopwatchRunning ? (getCurrentMillis() - stopwatchStartTicks) : stopwatchAccumulated;
                laps.push_back(elapsed);
            }
        }
    }
}

void renderStopwatch() {
    SDL_Color white = {255,255,255,255};
    SDL_Color gray  = {180,180,180,255};

    Uint32 elapsed = stopwatchRunning ? (getCurrentMillis() - stopwatchStartTicks) : stopwatchAccumulated;
    drawTextCentered("CRONÓMETRO", 0, 40, SCREEN_WIDTH, 60, font, white);
    drawTextCentered(formatStopwatch(elapsed), 0, 120, SCREEN_WIDTH, 200, bigFont, white);

    int startY = 340;
    int maxLaps = 5;
    int count = laps.size();
    for (int i = std::max(0, count - maxLaps); i < count; i++) {
        std::string lapStr = "Vuelta " + std::to_string(i+1) + ": " + formatStopwatch(laps[i]);
        drawTextCentered(lapStr, 400, startY, 480, 40, font, gray);
        startY += 45;
    }
    drawTextCentered("A: Inicio/Pausa  B: Reiniciar  X: Vuelta  SELECT: Salir", 0, 620, SCREEN_WIDTH, 40, font, gray);
}

// ----- Cuenta atrás -----
void processTimerInput(SDL_Event& e) {
    if (e.type == SDL_JOYBUTTONDOWN) {
        if (e.jbutton.button == BTN_A) { // A: inicio/pausa
            if (!timerRunning) {
                if (timerRemaining == 0) timerRemaining = timerSet * 1000;
                timerLastTick = getCurrentMillis();
                timerRunning = true;
            } else {
                timerRunning = false;
                timerRemaining -= (getCurrentMillis() - timerLastTick);
            }
        }
        if (e.jbutton.button == BTN_B) { // B: reset
            timerRunning = false;
            timerRemaining = 0;
        }
        if (e.jbutton.button == BTN_Y) { // Y: entrar/salir ajuste
            if (!timerRunning) {
                settingFocus = (settingFocus == FOCUS_NONE) ? FOCUS_TIMER_HOURS : FOCUS_NONE;
                if (settingFocus != FOCUS_NONE) timerRemaining = 0;
            }
        }
    }
    if (e.type == SDL_JOYHATMOTION && settingFocus != FOCUS_NONE && !timerRunning) {
        int val = e.jhat.value;
        int step = 1;
        if (settingFocus == FOCUS_TIMER_HOURS) step = 3600;
        else if (settingFocus == FOCUS_TIMER_MINUTES) step = 60;
        if (val & SDL_HAT_UP) timerSet = std::min(timerSet + step, 86399U);
        if (val & SDL_HAT_DOWN) timerSet = (timerSet >= step) ? timerSet - step : 0;
        if (val & SDL_HAT_RIGHT) {
            if (settingFocus == FOCUS_TIMER_HOURS) settingFocus = FOCUS_TIMER_MINUTES;
            else if (settingFocus == FOCUS_TIMER_MINUTES) settingFocus = FOCUS_TIMER_SECONDS;
        }
        if (val & SDL_HAT_LEFT) {
            if (settingFocus == FOCUS_TIMER_SECONDS) settingFocus = FOCUS_TIMER_MINUTES;
            else if (settingFocus == FOCUS_TIMER_MINUTES) settingFocus = FOCUS_TIMER_HOURS;
        }
    }
    if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_SPACE) {
            if (!timerRunning) {
                if (timerRemaining == 0) timerRemaining = timerSet * 1000;
                timerLastTick = getCurrentMillis();
                timerRunning = true;
            } else {
                timerRunning = false;
                timerRemaining -= (getCurrentMillis() - timerLastTick);
            }
        }
        if (e.key.keysym.sym == SDLK_r) {
            timerRunning = false;
            timerRemaining = 0;
        }
        if (e.key.keysym.sym == SDLK_s) {
            if (!timerRunning) {
                settingFocus = (settingFocus == FOCUS_NONE) ? FOCUS_TIMER_HOURS : FOCUS_NONE;
                if (settingFocus != FOCUS_NONE) timerRemaining = 0;
            }
        }
    }
}

void updateTimer() {
    if (timerRunning) {
        Uint32 now = getCurrentMillis();
        Uint32 elapsed = now - timerLastTick;
        if (elapsed >= timerRemaining) {
            timerRemaining = 0;
            timerRunning = false;
            alarmTriggered = true;
            beeper.start();
        } else {
            timerRemaining -= elapsed;
            timerLastTick = now;
        }
    }
}

void renderTimer() {
    SDL_Color white = {255,255,255,255};
    SDL_Color gray  = {180,180,180,255};
    SDL_Color yellow = {255,255,0,255};

    Uint32 displaySec = (timerRunning || timerRemaining > 0) ? (timerRemaining + 500) / 1000 : timerSet;
    int h = displaySec / 3600;
    int m = (displaySec % 3600) / 60;
    int s = displaySec % 60;
    std::string timeStr = formatTime(h, m, s);

    drawTextCentered("CUENTA ATRÁS", 0, 40, SCREEN_WIDTH, 60, font, white);
    drawTextCentered(timeStr, 0, 120, SCREEN_WIDTH, 200, bigFont, white);

    if (timerRunning) {
        drawTextCentered("En marcha...", 400, 320, 480, 40, font, gray);
    } else if (alarmTriggered) {
        drawTextCentered("¡TIEMPO! (Pulsa cualquier botón)", 400, 320, 480, 40, font, {255,0,0,255});
    }

    if (settingFocus != FOCUS_NONE && !timerRunning) {
        std::string setText = "Ajustando: " + timeStr;
        SDL_Rect setRect = {400, 420, 480, 50};
        SDL_Rect totalSize = getTextSize(setText, font);
        int startX = setRect.x + (setRect.w - totalSize.w) / 2;
        int startY = setRect.y + (setRect.h - totalSize.h) / 2;

        drawTextCentered(setText, setRect.x, setRect.y, setRect.w, setRect.h, font, yellow);

        std::string prefix = "Ajustando: ";
        SDL_Rect prefSize = getTextSize(prefix, font);
        SDL_Rect hSize = getTextSize(format2d(h), font);
        SDL_Rect mSize = getTextSize(format2d(m), font);
        SDL_Rect sSize = getTextSize(format2d(s), font);
        SDL_Rect colonSize = getTextSize(":", font);

        int xHour = startX + prefSize.w;
        int xMin = xHour + hSize.w + colonSize.w;
        int xSec = xMin + mSize.w + colonSize.w;

        SDL_Rect high;
        high.y = startY - 2;
        high.h = totalSize.h + 4;
        if (settingFocus == FOCUS_TIMER_HOURS) {
            high.x = xHour - 2;
            high.w = hSize.w + 4;
        } else if (settingFocus == FOCUS_TIMER_MINUTES) {
            high.x = xMin - 2;
            high.w = mSize.w + 4;
        } else {
            high.x = xSec - 2;
            high.w = sSize.w + 4;
        }

        // Resalte semitransparente
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 255, 255, 0, 80);
        SDL_RenderFillRect(renderer, &high);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    drawTextCentered("A: Inicio/Pausa  B: Reset  Y: Ajustar  SELECT: Salir", 0, 620, SCREEN_WIDTH, 40, font, gray);
}

// Inicialización y bucle principal
int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) < 0) {
        SDL_Log("Error SDL: %s", SDL_GetError());
        return 1;
    }
    if (TTF_Init() < 0) {
        SDL_Log("Error TTF: %s", TTF_GetError());
        SDL_Quit();
        return 1;
    }

    window = SDL_CreateWindow("Reloj", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
    if (!window) {
        SDL_Log("Error ventana: %s", SDL_GetError());
        TTF_Quit();
        SDL_Quit();
        return 1;
    }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        SDL_Log("Error renderer: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    const char* fontPath = "font.ttf";
    font = TTF_OpenFont(fontPath, 32);
    if (!font) font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 32);
    if (!font) {
        SDL_Log("No se pudo cargar la fuente.");
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }
    bigFont = TTF_OpenFont(fontPath, 96);
    if (!bigFont) bigFont = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 96);
    if (!bigFont) bigFont = font;

    SDL_Joystick* joystick = (SDL_NumJoysticks() > 0) ? SDL_JoystickOpen(0) : nullptr;

    bool running = true;
    SDL_Event event;
    SDL_Color bgColor = {20, 20, 40, 255};

    while (running) {
        while (SDL_PollEvent(&event)) {
            handleGlobalEvents(event, running);
            if (!running) break;

            // Cancelar alarma del timer en cualquier modo
            if (alarmTriggered && currentMode == MODE_TIMER) {
                if (event.type == SDL_JOYBUTTONDOWN || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_SPACE)) {
                    beeper.stop();
                    alarmTriggered = false;
                    timerRunning = false;
                    timerRemaining = 0;
                    continue;
                }
            }

            switch (currentMode) {
                case MODE_CLOCK:      processClockInput(event); break;
                case MODE_STOPWATCH:  processStopwatchInput(event); break;
                case MODE_TIMER:      processTimerInput(event); break;
            }
        }

        switch (currentMode) {
            case MODE_CLOCK:      updateClock(); break;
            case MODE_STOPWATCH:  break;
            case MODE_TIMER:      updateTimer(); break;
        }

        SDL_SetRenderDrawColor(renderer, bgColor.r, bgColor.g, bgColor.b, bgColor.a);
        SDL_RenderClear(renderer);

        std::string modeStr;
        if (currentMode == MODE_CLOCK) modeStr = "RELOJ";
        else if (currentMode == MODE_STOPWATCH) modeStr = "CRONÓMETRO";
        else modeStr = "CUENTA ATRÁS";
        drawTextCentered(modeStr, 0, 0, SCREEN_WIDTH, 35, font, {255,255,255,255});

        switch (currentMode) {
            case MODE_CLOCK:      renderClock(); break;
            case MODE_STOPWATCH:  renderStopwatch(); break;
            case MODE_TIMER:      renderTimer(); break;
        }

        drawTextCentered("L1/R1: Cambiar modo", 0, SCREEN_HEIGHT-30, SCREEN_WIDTH, 30, font, {150,150,150,255});

        SDL_RenderPresent(renderer);
        SDL_Delay(10);
    }

    beeper.close();
    if (bigFont != font) TTF_CloseFont(bigFont);
    TTF_CloseFont(font);
    if (joystick) SDL_JoystickClose(joystick);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}