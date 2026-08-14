#include "ringbuffer.h"
#include <stdlib.h>
#include <string.h>

RingBuffer *rb_create(size_t capacity_bytes) {
    RingBuffer *rb = calloc(1, sizeof(RingBuffer));
    if (!rb) return NULL;
    rb->data = malloc(capacity_bytes);
    if (!rb->data) { free(rb); return NULL; }
    rb->capacity = capacity_bytes;
    pthread_mutex_init(&rb->mutex, NULL);
    pthread_cond_init(&rb->cond_space, NULL);
    return rb;
}

void rb_destroy(RingBuffer *rb) {
    if (!rb) return;
    pthread_mutex_destroy(&rb->mutex);
    pthread_cond_destroy(&rb->cond_space);
    free(rb->data);
    free(rb);
}

void rb_reset(RingBuffer *rb) {
    pthread_mutex_lock(&rb->mutex);
    rb->read_pos = rb->write_pos = rb->fill = 0;
    rb->shutdown = 0; /* IMPORTANTE: sin esto, tras el primer decoder_close()
                          el buffer queda "apagado" para siempre y ninguna
                          pista siguiente puede escribir audio en el. */
    pthread_mutex_unlock(&rb->mutex);
}

void rb_shutdown(RingBuffer *rb) {
    pthread_mutex_lock(&rb->mutex);
    rb->shutdown = 1;
    pthread_cond_broadcast(&rb->cond_space);
    pthread_mutex_unlock(&rb->mutex);
}

size_t rb_write(RingBuffer *rb, const unsigned char *src, size_t len) {
    size_t written = 0;
    pthread_mutex_lock(&rb->mutex);
    while (written < len && !rb->shutdown) {
        size_t space = rb->capacity - rb->fill;
        if (space == 0) {
            pthread_cond_wait(&rb->cond_space, &rb->mutex);
            continue;
        }
        size_t chunk = len - written;
        if (chunk > space) chunk = space;
        size_t first = rb->capacity - rb->write_pos;
        if (first > chunk) first = chunk;
        memcpy(rb->data + rb->write_pos, src + written, first);
        if (chunk > first) {
            memcpy(rb->data, src + written + first, chunk - first);
        }
        rb->write_pos = (rb->write_pos + chunk) % rb->capacity;
        rb->fill += chunk;
        written += chunk;
    }
    pthread_mutex_unlock(&rb->mutex);
    return written;
}

size_t rb_read_or_silence(RingBuffer *rb, unsigned char *dst, size_t len) {
    size_t got = 0;
    pthread_mutex_lock(&rb->mutex);
    size_t avail = rb->fill;
    size_t chunk = (len < avail) ? len : avail;
    if (chunk > 0) {
        size_t first = rb->capacity - rb->read_pos;
        if (first > chunk) first = chunk;
        memcpy(dst, rb->data + rb->read_pos, first);
        if (chunk > first) {
            memcpy(dst + first, rb->data, chunk - first);
        }
        rb->read_pos = (rb->read_pos + chunk) % rb->capacity;
        rb->fill -= chunk;
        got = chunk;
        pthread_cond_broadcast(&rb->cond_space);
    }
    pthread_mutex_unlock(&rb->mutex);
    if (got < len) {
        memset(dst + got, 0, len - got); /* silencio para el resto */
    }
    return got;
}

size_t rb_fill(RingBuffer *rb) {
    pthread_mutex_lock(&rb->mutex);
    size_t f = rb->fill;
    pthread_mutex_unlock(&rb->mutex);
    return f;
}
