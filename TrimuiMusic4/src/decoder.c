#include "decoder.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

struct Decoder {
    AVFormatContext *fmt_ctx;
    AVCodecContext  *codec_ctx;
    SwrContext      *swr_ctx;
    int stream_idx;

    RingBuffer *rb;
    pthread_t thread;

    volatile int running;      /* hilo activo */
    volatile int stop_request; /* pedir cierre */
    volatile int paused;
    volatile int finished;

    volatile int seek_request;
    double seek_seconds;

    double duration;
    volatile double position; /* segundos aprox, actualizado por el hilo */

    pthread_mutex_t state_mutex;
};

static void *decode_thread_fn(void *arg);

Decoder *decoder_open(const char *path, RingBuffer *rb) {
    Decoder *dec = calloc(1, sizeof(Decoder));
    if (!dec) return NULL;
    dec->rb = rb;
    pthread_mutex_init(&dec->state_mutex, NULL);

    if (avformat_open_input(&dec->fmt_ctx, path, NULL, NULL) < 0) {
        fprintf(stderr, "No se pudo abrir %s\n", path);
        free(dec);
        return NULL;
    }
    if (avformat_find_stream_info(dec->fmt_ctx, NULL) < 0) {
        avformat_close_input(&dec->fmt_ctx);
        free(dec);
        return NULL;
    }

    dec->stream_idx = av_find_best_stream(dec->fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (dec->stream_idx < 0) {
        avformat_close_input(&dec->fmt_ctx);
        free(dec);
        return NULL;
    }

    AVStream *stream = dec->fmt_ctx->streams[dec->stream_idx];
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&dec->fmt_ctx);
        free(dec);
        return NULL;
    }

    dec->codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec->codec_ctx, stream->codecpar);
    if (avcodec_open2(dec->codec_ctx, codec, NULL) < 0) {
        avcodec_free_context(&dec->codec_ctx);
        avformat_close_input(&dec->fmt_ctx);
        free(dec);
        return NULL;
    }

    if (stream->duration != AV_NOPTS_VALUE) {
        dec->duration = stream->duration * av_q2d(stream->time_base);
    } else if (dec->fmt_ctx->duration != AV_NOPTS_VALUE) {
        dec->duration = dec->fmt_ctx->duration / (double)AV_TIME_BASE;
    }

    /* Resampler -> S16 estéreo interleaved 44100Hz */
    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, DECODER_OUT_CHANNELS);

    swr_alloc_set_opts2(&dec->swr_ctx,
        &out_layout, AV_SAMPLE_FMT_S16, DECODER_OUT_SAMPLERATE,
        &dec->codec_ctx->ch_layout, dec->codec_ctx->sample_fmt, dec->codec_ctx->sample_rate,
        0, NULL);

    if (!dec->swr_ctx || swr_init(dec->swr_ctx) < 0) {
        avcodec_free_context(&dec->codec_ctx);
        avformat_close_input(&dec->fmt_ctx);
        free(dec);
        return NULL;
    }

    dec->running = 1;
    if (pthread_create(&dec->thread, NULL, decode_thread_fn, dec) != 0) {
        dec->running = 0;
        swr_free(&dec->swr_ctx);
        avcodec_free_context(&dec->codec_ctx);
        avformat_close_input(&dec->fmt_ctx);
        free(dec);
        return NULL;
    }

    return dec;
}

static void *decode_thread_fn(void *arg) {
    Decoder *dec = (Decoder *)arg;
    AVPacket *pkt = av_packet_alloc();
    AVFrame  *frame = av_frame_alloc();
    AVStream *stream = dec->fmt_ctx->streams[dec->stream_idx];

    uint8_t *out_buf = NULL;
    int out_buf_samples = 0;

    while (!dec->stop_request) {

        if (dec->seek_request) {
            int64_t ts = (int64_t)(dec->seek_seconds / av_q2d(stream->time_base));
            av_seek_frame(dec->fmt_ctx, dec->stream_idx, ts, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(dec->codec_ctx);
            rb_reset(dec->rb);
            pthread_mutex_lock(&dec->state_mutex);
            dec->position = dec->seek_seconds;
            dec->seek_request = 0;
            dec->finished = 0;
            pthread_mutex_unlock(&dec->state_mutex);
        }

        if (dec->paused) {
            usleep(20000);
            continue;
        }

        int rd = av_read_frame(dec->fmt_ctx, pkt);
        if (rd < 0) {
            /* fin de archivo */
            pthread_mutex_lock(&dec->state_mutex);
            dec->finished = 1;
            pthread_mutex_unlock(&dec->state_mutex);
            usleep(50000);
            av_packet_unref(pkt);
            continue;
        }

        if (pkt->stream_index != dec->stream_idx) {
            av_packet_unref(pkt);
            continue;
        }

        if (avcodec_send_packet(dec->codec_ctx, pkt) == 0) {
            while (avcodec_receive_frame(dec->codec_ctx, frame) == 0) {
                int out_samples = swr_get_out_samples(dec->swr_ctx, frame->nb_samples);
                if (out_samples > out_buf_samples) {
                    av_freep(&out_buf);
                    out_buf_samples = out_samples;
                    av_samples_alloc(&out_buf, NULL, DECODER_OUT_CHANNELS,
                                      out_buf_samples, AV_SAMPLE_FMT_S16, 0);
                }
                int converted = swr_convert(dec->swr_ctx, &out_buf, out_samples,
                                             (const uint8_t **)frame->data, frame->nb_samples);
                if (converted > 0) {
                    size_t bytes = (size_t)converted * DECODER_OUT_CHANNELS * sizeof(int16_t);
                    rb_write(dec->rb, out_buf, bytes);
                }

                if (frame->pts != AV_NOPTS_VALUE) {
                    pthread_mutex_lock(&dec->state_mutex);
                    dec->position = frame->pts * av_q2d(stream->time_base);
                    pthread_mutex_unlock(&dec->state_mutex);
                }
                av_frame_unref(frame);
            }
        }
        av_packet_unref(pkt);
    }

    av_freep(&out_buf);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    dec->running = 0;
    return NULL;
}

void decoder_close(Decoder *dec) {
    if (!dec) return;
    dec->stop_request = 1;
    rb_shutdown(dec->rb);
    pthread_join(dec->thread, NULL);
    if (dec->swr_ctx) swr_free(&dec->swr_ctx);
    if (dec->codec_ctx) avcodec_free_context(&dec->codec_ctx);
    if (dec->fmt_ctx) avformat_close_input(&dec->fmt_ctx);
    pthread_mutex_destroy(&dec->state_mutex);
    free(dec);
}

void decoder_pause(Decoder *dec, int paused) { if (dec) dec->paused = paused; }
int decoder_is_paused(Decoder *dec) { return dec ? dec->paused : 0; }

void decoder_seek(Decoder *dec, double seconds) {
    if (!dec) return;
    if (seconds < 0) seconds = 0;
    dec->seek_seconds = seconds;
    dec->seek_request = 1;
}

double decoder_get_duration(Decoder *dec) { return dec ? dec->duration : 0; }

double decoder_get_position(Decoder *dec) {
    if (!dec) return 0;
    pthread_mutex_lock(&dec->state_mutex);
    double p = dec->position;
    pthread_mutex_unlock(&dec->state_mutex);
    return p;
}

int decoder_finished(Decoder *dec) {
    if (!dec) return 1;
    pthread_mutex_lock(&dec->state_mutex);
    int f = dec->finished;
    pthread_mutex_unlock(&dec->state_mutex);
    return f;
}
