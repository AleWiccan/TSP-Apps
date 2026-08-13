#ifndef AUDIO_OUT_H
#define AUDIO_OUT_H

#include "ringbuffer.h"
#include <SDL2/SDL.h>

#define VIS_SNAPSHOT_SAMPLES 1024 /* muestras mono recientes para el visualizador */

typedef struct {
    SDL_AudioDeviceID dev;
    RingBuffer *rb;

    int16_t vis_snapshot[VIS_SNAPSHOT_SAMPLES];
    SDL_mutex *vis_mutex;
} AudioOut;

AudioOut *audio_out_init(RingBuffer *rb);
void audio_out_close(AudioOut *ao);
void audio_out_set_pause(AudioOut *ao, int paused);

/* Copia una foto reciente de audio mono para visualizar (thread-safe) */
void audio_out_get_snapshot(AudioOut *ao, int16_t *dst, int n);

#endif
