#ifndef INPUT_H
#define INPUT_H

#include <SDL2/SDL.h>

typedef enum {
    ACTION_NONE = 0,
    ACTION_UP,
    ACTION_DOWN,
    ACTION_LEFT,
    ACTION_RIGHT,
    ACTION_CONFIRM,   /* A */
    ACTION_BACK,      /* B */
    ACTION_TOGGLE_VIS,/* X: mostrar/ocultar visualizador */
    ACTION_TOGGLE_LYRICS, /* Y: mostrar/ocultar letras */
    ACTION_QUIT       /* SELECT */
} InputAction;

/* Traduce un evento SDL (joystick o teclado) a una acción lógica.
 * Maneja botones, ejes (SDL_JOYAXISMOTION) y D-Pad tipo hat (SDL_JOYHATMOTION). */
InputAction input_translate_event(const SDL_Event *e);

#endif
