#include "player_core.h"
#include <string.h>

const char *basename_of(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

int skip_to_track(Browser *browser, int direction) {
    if (browser->count == 0) return 0;
    int start = browser->selected;
    do {
        if (direction > 0) browser_move_down(browser);
        else browser_move_up(browser);
    } while (browser->entries[browser->selected].is_dir && browser->selected != start);
    return !browser->entries[browser->selected].is_dir;
}

void load_and_play(const char *path, RingBuffer *rb, Decoder **dec,
                    AudioOut *audio, Lyrics *lyrics,
                    char *current_path, size_t current_path_cap,
                    int *is_paused) {
    if (*dec) { decoder_close(*dec); *dec = NULL; }
    rb_reset(rb);
    strncpy(current_path, path, current_path_cap - 1);
    current_path[current_path_cap - 1] = '\0';
    *dec = decoder_open(current_path, rb);
    *is_paused = 0;
    audio_out_set_pause(audio, 0);
    if (lyrics) lyrics_load_for_audio(lyrics, current_path);
}
