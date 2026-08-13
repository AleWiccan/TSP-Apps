#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include <stddef.h>
#include <pthread.h>

/* Ring buffer thread-safe de bytes, usado para PCM S16LE estéreo.
 * El hilo decodificador escribe, el callback de audio de SDL lee. */
typedef struct {
    unsigned char *data;
    size_t capacity;
    size_t read_pos;
    size_t write_pos;
    size_t fill;
    pthread_mutex_t mutex;
    pthread_cond_t cond_space;
    volatile int shutdown;
} RingBuffer;

RingBuffer *rb_create(size_t capacity_bytes);
void rb_destroy(RingBuffer *rb);
void rb_reset(RingBuffer *rb);

/* Bloquea si no hay espacio, hasta que el consumidor libere o shutdown=1 */
size_t rb_write(RingBuffer *rb, const unsigned char *src, size_t len);

/* No bloqueante: devuelve lo que haya disponible, rellena con silencio el resto */
size_t rb_read_or_silence(RingBuffer *rb, unsigned char *dst, size_t len);

size_t rb_fill(RingBuffer *rb);
void rb_shutdown(RingBuffer *rb);

#endif
