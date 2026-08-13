#ifndef DECODER_H
#define DECODER_H

#include "ringbuffer.h"

#define DECODER_OUT_SAMPLERATE 44100
#define DECODER_OUT_CHANNELS   2

typedef struct Decoder Decoder;

/* Abre el archivo y arranca el hilo de decodificación, que empuja
 * PCM S16LE estéreo 44100Hz al ring buffer dado. */
Decoder *decoder_open(const char *path, RingBuffer *rb);

void decoder_close(Decoder *dec);

void decoder_pause(Decoder *dec, int paused);
int  decoder_is_paused(Decoder *dec);

/* Pide un salto a esa posición en segundos (asíncrono) */
void decoder_seek(Decoder *dec, double seconds);

double decoder_get_duration(Decoder *dec);
double decoder_get_position(Decoder *dec);

/* 1 cuando terminó de decodificar todo el archivo (fin de pista) */
int decoder_finished(Decoder *dec);

#endif
