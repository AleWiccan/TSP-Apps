#include "browser.h"
#include "ui_theme.h"
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <strings.h>

static const char *AUDIO_EXTS[] = {
    ".mp3", ".wav", ".m4a", ".flac", ".ogg", ".aac", ".wma", ".opus", NULL
};

static int has_audio_ext(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    for (int i = 0; AUDIO_EXTS[i]; i++) {
        if (strcasecmp(dot, AUDIO_EXTS[i]) == 0) return 1;
    }
    return 0;
}

static int cmp_entries(const void *a, const void *b) {
    const BrowserEntry *ea = a, *eb = b;
    if (ea->is_dir != eb->is_dir) return eb->is_dir - ea->is_dir; /* carpetas primero */
    return strcasecmp(ea->name, eb->name);
}

static void reload_dir(Browser *br) {
    br->count = 0;
    br->selected = 0;
    br->scroll_offset = 0;

    DIR *d = opendir(br->current_dir);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && br->count < BROWSER_MAX_ENTRIES) {
        if (strcmp(ent->d_name, ".") == 0) continue;
        if (strcmp(ent->d_name, "..") == 0) continue;

        char full[BROWSER_PATH_LEN + 300];
        snprintf(full, sizeof(full), "%s/%s", br->current_dir, ent->d_name);

        struct stat st;
        if (stat(full, &st) != 0) continue;
        int is_dir = S_ISDIR(st.st_mode);

        if (!is_dir && !has_audio_ext(ent->d_name)) continue;

        strncpy(br->entries[br->count].name, ent->d_name, sizeof(br->entries[br->count].name) - 1);
        br->entries[br->count].is_dir = is_dir;
        br->count++;
    }
    closedir(d);

    qsort(br->entries, br->count, sizeof(BrowserEntry), cmp_entries);
}

void browser_init(Browser *br, const char *start_dir) {
    memset(br, 0, sizeof(Browser));
    strncpy(br->current_dir, start_dir, sizeof(br->current_dir) - 1);
    reload_dir(br);
}

void browser_move_up(Browser *br) {
    if (br->count == 0) return;
    br->selected = (br->selected - 1 + br->count) % br->count;
}

void browser_move_down(Browser *br) {
    if (br->count == 0) return;
    br->selected = (br->selected + 1) % br->count;
}

void browser_go_parent(Browser *br) {
    char *slash = strrchr(br->current_dir, '/');
    if (slash && slash != br->current_dir) {
        *slash = '\0';
        reload_dir(br);
    }
}

int browser_activate_selection(Browser *br, char *out_path, size_t out_size) {
    if (br->count == 0) return 0;
    BrowserEntry *sel = &br->entries[br->selected];

    if (sel->is_dir) {
        char newdir[BROWSER_PATH_LEN + 300];
        snprintf(newdir, sizeof(newdir), "%s/%s", br->current_dir, sel->name);
        strncpy(br->current_dir, newdir, sizeof(br->current_dir) - 1);
        reload_dir(br);
        return 0;
    } else {
        snprintf(out_path, out_size, "%s/%s", br->current_dir, sel->name);
        return 1;
    }
}

void browser_render(Browser *br, SDL_Renderer *rend, TTF_Font *font, SDL_Rect area) {
    int line_h = TTF_FontHeight(font) + 14;
    int visible = area.h / line_h;

    if (br->selected < br->scroll_offset) br->scroll_offset = br->selected;
    if (br->selected >= br->scroll_offset + visible) br->scroll_offset = br->selected - visible + 1;

    for (int row = 0; row < visible; row++) {
        int idx = br->scroll_offset + row;
        if (idx >= br->count) break;

        BrowserEntry *e = &br->entries[idx];
        SDL_Rect row_rect = { area.x, area.y + row * line_h, area.w, line_h };

        if (idx == br->selected) {
            SDL_SetRenderDrawColor(rend, COLOR_ACCENT_R, COLOR_ACCENT_G, COLOR_ACCENT_B, 60);
            SDL_RenderFillRect(rend, &row_rect);
            SDL_Rect bar = { area.x, row_rect.y, 6, line_h };
            SDL_SetRenderDrawColor(rend, COLOR_ACCENT_R, COLOR_ACCENT_G, COLOR_ACCENT_B, 255);
            SDL_RenderFillRect(rend, &bar);
        }

        char label[300];
        snprintf(label, sizeof(label), "%s %s", e->is_dir ? "[carpeta]" : "\xe2\x99\xaa", e->name);

        SDL_Color color = (idx == br->selected)
            ? (SDL_Color){COLOR_TEXT_R, COLOR_TEXT_G, COLOR_TEXT_B, 255}
            : (SDL_Color){COLOR_TEXT_DIM_R, COLOR_TEXT_DIM_G, COLOR_TEXT_DIM_B, 255};

        SDL_Surface *surf = TTF_RenderUTF8_Blended(font, label, color);
        if (surf) {
            SDL_Texture *tex = SDL_CreateTextureFromSurface(rend, surf);
            SDL_Rect dst = { area.x + 24, row_rect.y + (line_h - surf->h) / 2, surf->w, surf->h };
            SDL_RenderCopy(rend, tex, NULL, &dst);
            SDL_DestroyTexture(tex);
            SDL_FreeSurface(surf);
        }
    }
}
