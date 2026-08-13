#include "input.h"

/* NOTA: en el hardware real de esta Trimui Smart Pro, los indices de boton
 * A/B y X/Y estan intercambiados respecto a la tabla "teorica" del SDL de
 * Stock OS. Confirmado por pruebas directas en consola. */
#define BTN_A       1
#define BTN_B       0
#define BTN_X       3
#define BTN_Y       2
#define BTN_L1      4
#define BTN_R1      5
#define BTN_SELECT  8
#define BTN_START   9
#define BTN_DUP     11
#define BTN_DDOWN   12
#define BTN_DLEFT   13
#define BTN_DRIGHT  14

#define AXIS_DEADZONE 12000

InputAction input_translate_event(const SDL_Event *e) {
    switch (e->type) {
        case SDL_JOYBUTTONDOWN: {
            switch (e->jbutton.button) {
                case BTN_DUP:    return ACTION_UP;
                case BTN_DDOWN:  return ACTION_DOWN;
                case BTN_DLEFT:  return ACTION_LEFT;
                case BTN_DRIGHT: return ACTION_RIGHT;
                case BTN_A:      return ACTION_CONFIRM;
                case BTN_B:      return ACTION_BACK;
                case BTN_X:      return ACTION_TOGGLE_VIS;
                case BTN_Y:      return ACTION_TOGGLE_LYRICS;
                case BTN_SELECT: return ACTION_QUIT;
                default: return ACTION_NONE;
            }
        }
        case SDL_JOYHATMOTION: {
            Uint8 v = e->jhat.value;
            if (v & SDL_HAT_UP) return ACTION_UP;
            if (v & SDL_HAT_DOWN) return ACTION_DOWN;
            if (v & SDL_HAT_LEFT) return ACTION_LEFT;
            if (v & SDL_HAT_RIGHT) return ACTION_RIGHT;
            return ACTION_NONE;
        }
        case SDL_JOYAXISMOTION: {
            if (e->jaxis.axis == 0) {
                if (e->jaxis.value < -AXIS_DEADZONE) return ACTION_LEFT;
                if (e->jaxis.value > AXIS_DEADZONE) return ACTION_RIGHT;
            } else if (e->jaxis.axis == 1) {
                if (e->jaxis.value < -AXIS_DEADZONE) return ACTION_UP;
                if (e->jaxis.value > AXIS_DEADZONE) return ACTION_DOWN;
            }
            return ACTION_NONE;
        }
        /* Fallback de teclado, útil para probar en PC durante desarrollo */
        case SDL_KEYDOWN: {
            switch (e->key.keysym.sym) {
                case SDLK_UP:     return ACTION_UP;
                case SDLK_DOWN:   return ACTION_DOWN;
                case SDLK_LEFT:   return ACTION_LEFT;
                case SDLK_RIGHT:  return ACTION_RIGHT;
                case SDLK_RETURN: return ACTION_CONFIRM;
                case SDLK_BACKSPACE: return ACTION_BACK;
                case SDLK_v:      return ACTION_TOGGLE_VIS;
                case SDLK_l:      return ACTION_TOGGLE_LYRICS;
                case SDLK_ESCAPE: return ACTION_QUIT;
                default: return ACTION_NONE;
            }
        }
        default: return ACTION_NONE;
    }
}
