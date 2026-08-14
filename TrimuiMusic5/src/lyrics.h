#ifndef LYRICS_H
#define LYRICS_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#define LYRICS_MAX_LINES 2000
#define LYRICS_MAX_LEN   256

typedef struct {
    double time_sec;
    char text[LYRICS_MAX_LEN];
} LyricLine;

typedef struct {
    LyricLine lines[LYRICS_MAX_LINES];
    int count;
    int available; /* 1 si se encontró y cargó un .lrc */
} Lyrics;

/* audio_path: ruta del archivo de audio en reproducción.
 * Busca un .lrc con el mismo nombre base en la misma carpeta. */
void lyrics_load_for_audio(Lyrics *ly, const char *audio_path);
void lyrics_clear(Lyrics *ly);

/* Devuelve el índice de la línea actual según la posición de reproducción, o -1 */
int lyrics_current_index(const Lyrics *ly, double position_sec);

void lyrics_render(const Lyrics *ly, SDL_Renderer *rend, TTF_Font *font,
                    double position_sec, SDL_Rect area);

#endif
