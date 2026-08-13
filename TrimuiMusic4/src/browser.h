#ifndef BROWSER_H
#define BROWSER_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#define BROWSER_MAX_ENTRIES 1024
#define BROWSER_PATH_LEN 1024

typedef struct {
    char name[256];
    int is_dir;
} BrowserEntry;

typedef struct {
    char current_dir[BROWSER_PATH_LEN];
    BrowserEntry entries[BROWSER_MAX_ENTRIES];
    int count;
    int selected;
    int scroll_offset;
} Browser;

void browser_init(Browser *br, const char *start_dir);
void browser_move_up(Browser *br);
void browser_move_down(Browser *br);

/* Entra a carpeta seleccionada, o si es archivo, escribe su ruta completa en out_path
 * y devuelve 1. Si entra a carpeta devuelve 0. */
int browser_activate_selection(Browser *br, char *out_path, size_t out_size);

void browser_go_parent(Browser *br);

void browser_render(Browser *br, SDL_Renderer *rend, TTF_Font *font, SDL_Rect area);

#endif
