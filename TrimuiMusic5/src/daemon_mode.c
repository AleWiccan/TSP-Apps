#include "daemon_mode.h"
#include "player_core.h"
#include <SDL2/SDL.h>
#include <libavformat/avformat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>

int run_headless_daemon(const char *folder, const char *file, double start_pos, int start_paused) {
    av_log_set_level(AV_LOG_QUIET);

    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        return 1;
    }

    FILE *pidf = fopen(DAEMON_PID_FILE, "w");
    if (pidf) {
        fprintf(pidf, "%d", (int)getpid());
        fclose(pidf);
    }

    Browser browser;
    browser_init(&browser, folder);

    /* Ubicar la seleccion en el archivo que veniamos reproduciendo, para que
     * el auto-avance a la siguiente pista continue desde ahi. */
    const char *base = basename_of(file);
    for (int i = 0; i < browser.count; i++) {
        if (!browser.entries[i].is_dir && strcmp(browser.entries[i].name, base) == 0) {
            browser.selected = i;
            break;
        }
    }

    RingBuffer *rb = rb_create(RB_CAPACITY_BYTES);
    AudioOut *audio = audio_out_init(rb);
    if (!rb || !audio) {
        remove(DAEMON_PID_FILE);
        SDL_Quit();
        return 1;
    }

    Decoder *dec = decoder_open(file, rb);
    int is_paused = start_paused;
    if (dec) {
        if (start_pos > 0.5) decoder_seek(dec, start_pos);
        decoder_pause(dec, is_paused);
        audio_out_set_pause(audio, is_paused);
    }

    char current_path[BROWSER_PATH_LEN];
    strncpy(current_path, file, sizeof(current_path) - 1);
    current_path[sizeof(current_path) - 1] = '\0';

    while (dec) {
        if (decoder_finished(dec)) {
            char next_path[BROWSER_PATH_LEN];
            if (skip_to_track(&browser, +1) &&
                browser_activate_selection(&browser, next_path, sizeof(next_path))) {
                load_and_play(next_path, rb, &dec, audio, NULL,
                               current_path, sizeof(current_path), &is_paused);
            } else {
                decoder_close(dec);
                dec = NULL;
            }
        }
        SDL_Delay(200);
    }

    audio_out_close(audio);
    rb_destroy(rb);
    remove(DAEMON_PID_FILE);
    SDL_Quit();
    return 0;
}

int spawn_background_daemon(const char *folder, const char *file, double pos, int paused) {
    char exe_path[1024];
    ssize_t n = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (n <= 0) return 0; /* no se pudo determinar el propio ejecutable */
    exe_path[n] = '\0';

    pid_t pid1 = fork();
    if (pid1 < 0) return 0;
    if (pid1 > 0) return 1; /* proceso original: sigue su cierre normal */

    /* Primer hijo: iniciar nueva sesion y hacer un segundo fork para
     * desprenderse por completo (double-fork daemonize estandar en Unix). */
    setsid();
    pid_t pid2 = fork();
    if (pid2 < 0) _exit(1);
    if (pid2 > 0) _exit(0);

    /* Nieto: el verdadero proceso de fondo. */
    chdir("/");
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > 2) close(devnull);
    }

    char pos_str[32];
    snprintf(pos_str, sizeof(pos_str), "%.3f", pos);

    execl(exe_path, exe_path, "--daemon", folder, file, pos_str, paused ? "1" : "0", (char *)NULL);
    _exit(1); /* solo se llega aqui si execl fallo */
}

long detect_running_daemon(void) {
    FILE *f = fopen(DAEMON_PID_FILE, "r");
    if (!f) return -1;
    long pid = -1;
    if (fscanf(f, "%ld", &pid) != 1) pid = -1;
    fclose(f);
    if (pid <= 0) return -1;
    if (kill((pid_t)pid, 0) != 0) return -1; /* proceso no existe */
    return pid;
}

void stop_background_daemon(long pid) {
    if (pid > 0) {
        kill((pid_t)pid, SIGTERM);
        usleep(150000); /* pequeño margen para una salida limpia */
        if (kill((pid_t)pid, 0) == 0) {
            kill((pid_t)pid, SIGKILL); /* no respondio a tiempo, forzar */
        }
    }
    remove(DAEMON_PID_FILE);
}
