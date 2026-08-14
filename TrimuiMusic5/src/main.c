#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavformat/avformat.h>

#include "ui_theme.h"
#include "ringbuffer.h"
#include "decoder.h"
#include "audio_out.h"
#include "visualizer.h"
#include "lyrics.h"
#include "browser.h"
#include "input.h"
#include "player_core.h"
#include "daemon_mode.h"

#define MUSIC_DIR_DEFAULT "/mnt/SDCARD/Music"

typedef enum { SCREEN_BROWSER, SCREEN_PLAYER } ScreenState;

static const char *find_font_path(void) {
    static const char *candidates[] = {
        "./fonts/NotoSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        NULL
    };
    for (int i = 0; candidates[i]; i++) {
        FILE *f = fopen(candidates[i], "rb");
        if (f) { fclose(f); return candidates[i]; }
    }
    return NULL;
}

static void draw_text(SDL_Renderer *rend, TTF_Font *font, const char *text,
                       int x, int y, SDL_Color color) {
    if (!text || !text[0]) return;
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, color);
    if (!surf) return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(rend, surf);
    SDL_Rect dst = { x, y, surf->w, surf->h };
    SDL_RenderCopy(rend, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

static void format_time(double seconds, char *out, size_t out_size) {
    if (seconds < 0) seconds = 0;
    int m = (int)seconds / 60;
    int s = (int)seconds % 60;
    snprintf(out, out_size, "%d:%02d", m, s);
}

int main(int argc, char **argv) {
    /* Modo daemon headless: el propio binario se relanza a si mismo con este
     * flag (ver spawn_background_daemon en daemon_mode.c) para seguir
     * reproduciendo de forma independiente cuando se sale de la app. */
    if (argc >= 5 && strcmp(argv[1], "--daemon") == 0) {
        double pos = atof(argv[4]);
        int paused = (argc >= 6 && strcmp(argv[5], "1") == 0);
        return run_headless_daemon(argv[2], argv[3], pos, paused);
    }

    av_log_set_level(AV_LOG_QUIET);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) != 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init error: %s\n", TTF_GetError());
        return 1;
    }

    /* Evitar que el sistema apague la pantalla por inactividad de touch/mouse.
     * Nota: en algunos Stock OS el auto-apagado se controla por un daemon externo
     * basado en eventos de input; si persiste, puede requerirse tocar algún
     * mecanismo específico del firmware (revisar /etc/init.d o similar). */
    SDL_DisableScreenSaver();

    SDL_Joystick *joy = NULL;
    if (SDL_NumJoysticks() > 0) {
        joy = SDL_JoystickOpen(0);
    }

    SDL_Window *win = SDL_CreateWindow("TrimuiMusic",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!win) {
        fprintf(stderr, "CreateWindow error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Renderer *rend = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!rend) {
        rend = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    }
    SDL_RenderSetLogicalSize(rend, SCREEN_W, SCREEN_H);

    const char *font_path = find_font_path();
    if (!font_path) {
        fprintf(stderr, "No se encontro ninguna fuente TTF (fonts/NotoSans-Bold.ttf)\n");
        return 1;
    }
    TTF_Font *font_title = TTF_OpenFont(font_path, 34);
    TTF_Font *font_normal = TTF_OpenFont(font_path, 24);
    TTF_Font *font_lyrics = TTF_OpenFont(font_path, 28);

    /* --- Estado de la app --- */
    Browser browser;
    browser_init(&browser, MUSIC_DIR_DEFAULT);

    ScreenState screen = SCREEN_BROWSER;

    RingBuffer *rb = rb_create(RB_CAPACITY_BYTES);
    AudioOut *audio = audio_out_init(rb);
    Decoder *dec = NULL;

    Visualizer vis;
    visualizer_init(&vis);
    int show_visualizer = 1;

    Lyrics lyrics;
    lyrics_clear(&lyrics);
    int show_lyrics = 1;

    char current_path[BROWSER_PATH_LEN] = {0};
    int is_paused = 0;

    /* Si ya hay un daemon de musica en 2do plano corriendo (de una sesion
     * anterior de esta misma app), lo detectamos para poder mostrarlo/
     * detenerlo, aunque esta instancia todavia no reproduzca nada localmente. */
    long bg_daemon_pid = detect_running_daemon();

    int running = 1;
    Uint32 last_ticks = SDL_GetTicks();

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = 0; break; }

            InputAction act = input_translate_event(&e);

            if (screen == SCREEN_BROWSER) {
                switch (act) {
                    case ACTION_UP:   browser_move_up(&browser); break;
                    case ACTION_DOWN: browser_move_down(&browser); break;
                    case ACTION_BACK: browser_go_parent(&browser); break;
                    case ACTION_QUIT:
                        if (dec && !is_paused) {
                            spawn_background_daemon(browser.current_dir, current_path,
                                                     decoder_get_position(dec), 0);
                        }
                        running = 0;
                        break;
                    case ACTION_STOP_BG:
                        if (bg_daemon_pid > 0) {
                            stop_background_daemon(bg_daemon_pid);
                            bg_daemon_pid = -1;
                        }
                        break;
                    /* Mini-reproductor: la musica sigue sonando en segundo
                     * plano mientras navegas el explorador (el decoder corre
                     * en su propio hilo). Estos atajos permiten controlarla
                     * sin tener que volver a buscar la cancion.
                     * Si no hay nada cargado localmente pero SI hay un daemon
                     * de una sesion anterior sonando, X lo detiene (en vez de
                     * depender de L1, cuyo indice de boton no esta
                     * confirmado en este hardware). */
                    case ACTION_TOGGLE_VIS:
                        if (dec) {
                            is_paused = !is_paused;
                            decoder_pause(dec, is_paused);
                            audio_out_set_pause(audio, is_paused);
                        } else if (bg_daemon_pid > 0) {
                            stop_background_daemon(bg_daemon_pid);
                            bg_daemon_pid = -1;
                        }
                        break;
                    case ACTION_TOGGLE_LYRICS:
                        if (dec) screen = SCREEN_PLAYER;
                        break;
                    case ACTION_CONFIRM: {
                        char path[BROWSER_PATH_LEN];
                        if (browser_activate_selection(&browser, path, sizeof(path))) {
                            /* Si habia musica sonando en 2do plano de una
                             * sesion anterior, detenerla para que no suene
                             * junto con la nueva seleccion. */
                            if (bg_daemon_pid > 0) {
                                stop_background_daemon(bg_daemon_pid);
                                bg_daemon_pid = -1;
                            }
                            load_and_play(path, rb, &dec, audio, &lyrics,
                                          current_path, sizeof(current_path), &is_paused);
                            screen = SCREEN_PLAYER;
                        }
                        break;
                    }
                    default: break;
                }
            } else { /* SCREEN_PLAYER */
                switch (act) {
                    case ACTION_BACK: screen = SCREEN_BROWSER; break;
                    case ACTION_QUIT:
                        if (dec && !is_paused) {
                            spawn_background_daemon(browser.current_dir, current_path,
                                                     decoder_get_position(dec), 0);
                        }
                        running = 0;
                        break;
                    case ACTION_CONFIRM: {
                        is_paused = !is_paused;
                        decoder_pause(dec, is_paused);
                        audio_out_set_pause(audio, is_paused);
                        break;
                    }
                    case ACTION_LEFT:
                        if (dec) decoder_seek(dec, decoder_get_position(dec) - 10.0);
                        break;
                    case ACTION_RIGHT:
                        if (dec) decoder_seek(dec, decoder_get_position(dec) + 10.0);
                        break;
                    case ACTION_UP: {
                        char path[BROWSER_PATH_LEN];
                        if (skip_to_track(&browser, -1) &&
                            browser_activate_selection(&browser, path, sizeof(path))) {
                            load_and_play(path, rb, &dec, audio, &lyrics,
                                          current_path, sizeof(current_path), &is_paused);
                        }
                        break;
                    }
                    case ACTION_DOWN: {
                        char path[BROWSER_PATH_LEN];
                        if (skip_to_track(&browser, +1) &&
                            browser_activate_selection(&browser, path, sizeof(path))) {
                            load_and_play(path, rb, &dec, audio, &lyrics,
                                          current_path, sizeof(current_path), &is_paused);
                        }
                        break;
                    }
                    case ACTION_TOGGLE_VIS:
                        show_visualizer = !show_visualizer;
                        vis.enabled = show_visualizer;
                        break;
                    case ACTION_TOGGLE_LYRICS:
                        show_lyrics = !show_lyrics;
                        break;
                    default: break;
                }
            }
        }

        /* Avanzar a la siguiente pista automáticamente si termino */
        if (dec && decoder_finished(dec)) {
            char next_path[BROWSER_PATH_LEN];
            if (skip_to_track(&browser, +1) &&
                browser_activate_selection(&browser, next_path, sizeof(next_path))) {
                load_and_play(next_path, rb, &dec, audio, &lyrics,
                              current_path, sizeof(current_path), &is_paused);
            } else {
                decoder_close(dec); dec = NULL;
            }
        }

        /* Actualizar visualizador (barato: solo si esta habilitado) */
        if (show_visualizer && dec) {
            int16_t snapshot[VIS_FFT_SIZE];
            audio_out_get_snapshot(audio, snapshot, VIS_FFT_SIZE);
            visualizer_update(&vis, snapshot, VIS_FFT_SIZE);
        }

        /* --- Render --- */
        SDL_SetRenderDrawColor(rend, COLOR_BG_R, COLOR_BG_G, COLOR_BG_B, 255);
        SDL_RenderClear(rend);

        if (screen == SCREEN_BROWSER) {
            SDL_Color title_color = { COLOR_TEXT_R, COLOR_TEXT_G, COLOR_TEXT_B, 255 };
            draw_text(rend, font_title, "TrimuiMusic", 40, 30, title_color);
            SDL_Color dim = { COLOR_TEXT_DIM_R, COLOR_TEXT_DIM_G, COLOR_TEXT_DIM_B, 255 };
            draw_text(rend, font_normal, browser.current_dir, 40, 80, dim);

            if (!dec && bg_daemon_pid > 0) {
                char bgmsg[128];
                snprintf(bgmsg, sizeof(bgmsg), "\xe2\x99\xaa Musica sonando en 2do plano (PID %ld) - [X] Detener", bg_daemon_pid);
                SDL_Color accent_color = { COLOR_ACCENT_R, COLOR_ACCENT_G, COLOR_ACCENT_B, 255 };
                draw_text(rend, font_normal, bgmsg, 40, 105, accent_color);
            }

            int minibar_h = dec ? 80 : 0;
            SDL_Rect list_area = { 40, 130, SCREEN_W - 80, SCREEN_H - 180 - minibar_h };
            browser_render(&browser, rend, font_normal, list_area);

            if (dec) {
                /* Barra "reproduciendo ahora": confirma que el audio sigue
                 * sonando en segundo plano aunque estes navegando el
                 * explorador, y permite pausar / volver al reproductor. */
                SDL_Rect bar = { 40, SCREEN_H - 110, SCREEN_W - 80, 70 };
                SDL_SetRenderDrawColor(rend, COLOR_ACCENT_R, COLOR_ACCENT_G, COLOR_ACCENT_B, 40);
                SDL_RenderFillRect(rend, &bar);
                SDL_Rect accent_edge = { bar.x, bar.y, 6, bar.h };
                SDL_SetRenderDrawColor(rend, COLOR_ACCENT_R, COLOR_ACCENT_G, COLOR_ACCENT_B, 255);
                SDL_RenderFillRect(rend, &accent_edge);

                char now_playing[320];
                snprintf(now_playing, sizeof(now_playing), "%s %s",
                         is_paused ? "\xe2\x8f\xb8" : "\xe2\x99\xaa", basename_of(current_path));
                SDL_Color text_color = { COLOR_TEXT_R, COLOR_TEXT_G, COLOR_TEXT_B, 255 };
                draw_text(rend, font_normal, now_playing, bar.x + 24, bar.y + 10, text_color);
                draw_text(rend, font_normal, "[X] Pausa   [Y] Abrir reproductor",
                          bar.x + 24, bar.y + 40, dim);
            }

        } else {
            SDL_Color title_color = { COLOR_TEXT_R, COLOR_TEXT_G, COLOR_TEXT_B, 255 };
            draw_text(rend, font_title, basename_of(current_path), 40, 30, title_color);

            double pos = dec ? decoder_get_position(dec) : 0;
            double dur = dec ? decoder_get_duration(dec) : 0;
            char t_pos[16], t_dur[16];
            format_time(pos, t_pos, sizeof(t_pos));
            format_time(dur, t_dur, sizeof(t_dur));

            /* barra de progreso */
            SDL_Rect bar_bg = { 40, 100, SCREEN_W - 80, 8 };
            SDL_SetRenderDrawColor(rend, 50, 45, 65, 255);
            SDL_RenderFillRect(rend, &bar_bg);
            if (dur > 0) {
                SDL_Rect bar_fg = bar_bg;
                bar_fg.w = (int)((pos / dur) * bar_bg.w);
                SDL_SetRenderDrawColor(rend, COLOR_ACCENT_R, COLOR_ACCENT_G, COLOR_ACCENT_B, 255);
                SDL_RenderFillRect(rend, &bar_fg);
            }
            char time_label[64];
            snprintf(time_label, sizeof(time_label), "%s / %s%s", t_pos, t_dur,
                     is_paused ? "   [PAUSA]" : "");
            SDL_Color dim = { COLOR_TEXT_DIM_R, COLOR_TEXT_DIM_G, COLOR_TEXT_DIM_B, 255 };
            draw_text(rend, font_normal, time_label, 40, 118, dim);

            if (show_lyrics && lyrics.available) {
                SDL_Rect lyrics_area = { 40, 170, SCREEN_W - 80, 260 };
                lyrics_render(&lyrics, rend, font_lyrics, pos, lyrics_area);
            }

            if (show_visualizer) {
                SDL_Rect vis_area = { 40, SCREEN_H - 220, SCREEN_W - 80, 160 };
                visualizer_render(&vis, rend, vis_area);
            }

            /* hints de botones en pantalla (sin touch, se activan con el mando) */
            char hint[160];
            snprintf(hint, sizeof(hint), "[A] Pausa [Arr/Abj] Ant/Sig [X] Vis: %s [Y] Letra: %s [B] Volver",
                     show_visualizer ? "ON" : "OFF", show_lyrics ? "ON" : "OFF");
            draw_text(rend, font_normal, hint, 40, SCREEN_H - 40, dim);
        }

        SDL_RenderPresent(rend);

        /* Limitar a ~60fps para no gastar CPU/batería innecesariamente */
        Uint32 now = SDL_GetTicks();
        Uint32 elapsed = now - last_ticks;
        if (elapsed < 16) SDL_Delay(16 - elapsed);
        last_ticks = SDL_GetTicks();
    }

    if (dec) decoder_close(dec);
    audio_out_close(audio);
    rb_destroy(rb);

    if (joy) SDL_JoystickClose(joy);
    TTF_CloseFont(font_title);
    TTF_CloseFont(font_normal);
    TTF_CloseFont(font_lyrics);
    TTF_Quit();
    SDL_DestroyRenderer(rend);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
