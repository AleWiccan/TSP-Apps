#include "lyrics.h"
#include "ui_theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void build_lrc_path(const char *audio_path, char *out, size_t out_size) {
    strncpy(out, audio_path, out_size - 1);
    out[out_size - 1] = '\0';
    char *dot = strrchr(out, '.');
    char *slash = strrchr(out, '/');
    if (dot && (!slash || dot > slash)) {
        strcpy(dot, ".lrc");
    } else {
        strncat(out, ".lrc", out_size - strlen(out) - 1);
    }
}

/* Parsea líneas tipo: [00:12.34]Texto de la letra
 * Soporta múltiples timestamps en la misma línea: [00:12.34][00:45.00]Texto */
static int parse_time_tag(const char *p, double *out_sec, const char **end) {
    int min, sec, cs = 0;
    const char *start = p;
    if (*p != '[') return 0;
    p++;
    if (sscanf(p, "%d:%d.%d", &min, &sec, &cs) < 2) {
        if (sscanf(p, "%d:%d", &min, &sec) < 2) return 0;
    }
    const char *close = strchr(p, ']');
    if (!close) return 0;
    *out_sec = min * 60.0 + sec + cs / 100.0;
    *end = close + 1;
    (void)start;
    return 1;
}

void lyrics_load_for_audio(Lyrics *ly, const char *audio_path) {
    memset(ly, 0, sizeof(Lyrics));
    char lrc_path[1024];
    build_lrc_path(audio_path, lrc_path, sizeof(lrc_path));

    FILE *f = fopen(lrc_path, "r");
    if (!f) return;

    char line[1024];
    while (fgets(line, sizeof(line), f) && ly->count < LYRICS_MAX_LINES) {
        const char *p = line;
        double times[8];
        int n_times = 0;
        while (*p == '[' && n_times < 8) {
            double t;
            const char *end;
            if (parse_time_tag(p, &t, &end)) {
                /* descartar metadatos tipo [ar:], [ti:], [al:] (no son numéricos) */
                if (isdigit((unsigned char)p[1])) {
                    times[n_times++] = t;
                }
                p = end;
            } else {
                break;
            }
        }
        if (n_times == 0) continue;

        /* limpiar salto de línea */
        char text[LYRICS_MAX_LEN];
        strncpy(text, p, sizeof(text) - 1);
        text[sizeof(text) - 1] = '\0';
        size_t len = strlen(text);
        while (len > 0 && (text[len-1] == '\n' || text[len-1] == '\r')) {
            text[--len] = '\0';
        }

        for (int i = 0; i < n_times && ly->count < LYRICS_MAX_LINES; i++) {
            ly->lines[ly->count].time_sec = times[i];
            strncpy(ly->lines[ly->count].text, text, LYRICS_MAX_LEN - 1);
            ly->count++;
        }
    }
    fclose(f);

    /* ordenar por tiempo (insertion sort, listas de letras son pequeñas) */
    for (int i = 1; i < ly->count; i++) {
        LyricLine key = ly->lines[i];
        int j = i - 1;
        while (j >= 0 && ly->lines[j].time_sec > key.time_sec) {
            ly->lines[j + 1] = ly->lines[j];
            j--;
        }
        ly->lines[j + 1] = key;
    }

    ly->available = (ly->count > 0);
}

void lyrics_clear(Lyrics *ly) {
    memset(ly, 0, sizeof(Lyrics));
}

int lyrics_current_index(const Lyrics *ly, double position_sec) {
    if (!ly->available) return -1;
    int idx = -1;
    /* búsqueda lineal simple: listas de letras son pequeñas (cientos de líneas) */
    for (int i = 0; i < ly->count; i++) {
        if (ly->lines[i].time_sec <= position_sec) idx = i;
        else break;
    }
    return idx;
}

void lyrics_render(const Lyrics *ly, SDL_Renderer *rend, TTF_Font *font,
                    double position_sec, SDL_Rect area) {
    if (!ly->available) return;
    int cur = lyrics_current_index(ly, position_sec);
    if (cur < 0) return;

    int line_h = TTF_FontHeight(font) + 10;
    int center_y = area.y + area.h / 2;

    /* Mostrar 2 líneas antes, la actual centrada y resaltada, y 2 después */
    for (int offset = -2; offset <= 2; offset++) {
        int idx = cur + offset;
        if (idx < 0 || idx >= ly->count) continue;

        SDL_Color color;
        if (offset == 0) {
            color.r = COLOR_ACCENT2_R; color.g = COLOR_ACCENT2_G; color.b = COLOR_ACCENT2_B;
        } else {
            int fade = 255 - abs(offset) * 60;
            color.r = color.g = color.b = (Uint8)(fade > 60 ? fade : 60);
        }
        color.a = 255;

        SDL_Surface *surf = TTF_RenderUTF8_Blended(font, ly->lines[idx].text, color);
        if (!surf) continue;
        SDL_Texture *tex = SDL_CreateTextureFromSurface(rend, surf);
        SDL_Rect dst;
        dst.w = surf->w; dst.h = surf->h;
        dst.x = area.x + (area.w - dst.w) / 2;
        dst.y = center_y + offset * line_h - dst.h / 2;
        SDL_RenderCopy(rend, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(surf);
    }
}
