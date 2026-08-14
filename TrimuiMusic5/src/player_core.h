#ifndef PLAYER_CORE_H
#define PLAYER_CORE_H

#include <stddef.h>
#include "browser.h"
#include "ringbuffer.h"
#include "decoder.h"
#include "audio_out.h"
#include "lyrics.h"

#define RB_CAPACITY_BYTES (DECODER_OUT_SAMPLERATE * DECODER_OUT_CHANNELS * sizeof(int16_t) * 3) /* ~3s de buffer */

const char *basename_of(const char *path);

/* Mueve la seleccion del explorador 'direction' pasos (+1 siguiente, -1
 * anterior), saltando carpetas. Devuelve 1 si la seleccion resultante es un
 * archivo reproducible (0 si no hay ninguna pista disponible en la carpeta). */
int skip_to_track(Browser *browser, int direction);

/* Cierra el decoder actual (si hay) y arranca la reproduccion de 'path'.
 * 'lyrics' puede ser NULL (modo headless/daemon, sin letras). */
void load_and_play(const char *path, RingBuffer *rb, Decoder **dec,
                    AudioOut *audio, Lyrics *lyrics,
                    char *current_path, size_t current_path_cap,
                    int *is_paused);

#endif
