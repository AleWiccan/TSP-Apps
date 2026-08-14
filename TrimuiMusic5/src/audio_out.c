#include "audio_out.h"
#include "decoder.h"
#include <string.h>
#include <stdlib.h>

static void audio_callback(void *userdata, Uint8 *stream, int len) {
    AudioOut *ao = (AudioOut *)userdata;
    rb_read_or_silence(ao->rb, stream, (size_t)len);

    /* Actualizar snapshot mono para el visualizador (barato: solo downmix simple) */
    int16_t *samples = (int16_t *)stream;
    int n_frames = len / (int)sizeof(int16_t) / DECODER_OUT_CHANNELS;

    SDL_LockMutex(ao->vis_mutex);
    int copy_n = n_frames < VIS_SNAPSHOT_SAMPLES ? n_frames : VIS_SNAPSHOT_SAMPLES;
    for (int i = 0; i < copy_n; i++) {
        int16_t l = samples[i * DECODER_OUT_CHANNELS];
        int16_t r = samples[i * DECODER_OUT_CHANNELS + 1];
        ao->vis_snapshot[i] = (int16_t)(((int)l + (int)r) / 2);
    }
    SDL_UnlockMutex(ao->vis_mutex);
}

AudioOut *audio_out_init(RingBuffer *rb) {
    AudioOut *ao = calloc(1, sizeof(AudioOut));
    if (!ao) return NULL;
    ao->rb = rb;
    ao->vis_mutex = SDL_CreateMutex();

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = DECODER_OUT_SAMPLERATE;
    want.format = AUDIO_S16SYS;
    want.channels = DECODER_OUT_CHANNELS;
    want.samples = 2048;
    want.callback = audio_callback;
    want.userdata = ao;

    ao->dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (ao->dev == 0) {
        SDL_DestroyMutex(ao->vis_mutex);
        free(ao);
        return NULL;
    }
    SDL_PauseAudioDevice(ao->dev, 0);
    return ao;
}

void audio_out_close(AudioOut *ao) {
    if (!ao) return;
    SDL_CloseAudioDevice(ao->dev);
    SDL_DestroyMutex(ao->vis_mutex);
    free(ao);
}

void audio_out_set_pause(AudioOut *ao, int paused) {
    if (!ao) return;
    SDL_PauseAudioDevice(ao->dev, paused);
}

void audio_out_get_snapshot(AudioOut *ao, int16_t *dst, int n) {
    if (!ao) { memset(dst, 0, n * sizeof(int16_t)); return; }
    SDL_LockMutex(ao->vis_mutex);
    if (n > VIS_SNAPSHOT_SAMPLES) n = VIS_SNAPSHOT_SAMPLES;
    memcpy(dst, ao->vis_snapshot, n * sizeof(int16_t));
    SDL_UnlockMutex(ao->vis_mutex);
}
