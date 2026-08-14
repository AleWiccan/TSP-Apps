#include "visualizer.h"
#include "ui_theme.h"
#include "decoder.h"
#include <math.h>
#include <string.h>

static void fft(float *re, float *im, int n) {
    /* Cooley-Tukey iterativo, in-place, n potencia de 2 */
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float tr = re[i]; re[i] = re[j]; re[j] = tr;
            float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * M_PI / len;
        float wr = (float)cos(ang), wi = (float)sin(ang);
        for (int i = 0; i < n; i += len) {
            float cur_wr = 1.0f, cur_wi = 0.0f;
            for (int k = 0; k < len / 2; k++) {
                float ur = re[i + k], ui = im[i + k];
                float vr = re[i + k + len / 2] * cur_wr - im[i + k + len / 2] * cur_wi;
                float vi = re[i + k + len / 2] * cur_wi + im[i + k + len / 2] * cur_wr;
                re[i + k] = ur + vr;
                im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr;
                im[i + k + len / 2] = ui - vi;
                float next_wr = cur_wr * wr - cur_wi * wi;
                float next_wi = cur_wr * wi + cur_wi * wr;
                cur_wr = next_wr; cur_wi = next_wi;
            }
        }
    }
}

void visualizer_init(Visualizer *vis) {
    memset(vis, 0, sizeof(Visualizer));
    vis->enabled = 1;
}

void visualizer_update(Visualizer *vis, const int16_t *samples, int n_samples) {
    if (!vis->enabled) return;
    int n = VIS_FFT_SIZE;
    if (n_samples < n) n = n_samples;

    /* Ventana de Hann + normalizar a [-1,1] */
    for (int i = 0; i < VIS_FFT_SIZE; i++) {
        float s = (i < n) ? (samples[i] / 32768.0f) : 0.0f;
        float w = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (VIS_FFT_SIZE - 1));
        vis->fft_real[i] = s * w;
        vis->fft_imag[i] = 0.0f;
    }

    fft(vis->fft_real, vis->fft_imag, VIS_FFT_SIZE);

    /* Mapear a VIS_BARS bandas logarítmicas, acotado a un rango de frecuencia
     * musicalmente útil (hasta ~9kHz) en vez de hasta Nyquist completo
     * (~22kHz): casi ninguna música tiene energía relevante por encima de eso,
     * así que mapear hasta ahí dejaba las últimas barras siempre "muertas". */
    int half = VIS_FFT_SIZE / 2;
    float hz_per_bin = (float)DECODER_OUT_SAMPLERATE / VIS_FFT_SIZE;
    int max_bin = (int)(9000.0f / hz_per_bin);
    if (max_bin < 8) max_bin = 8;
    if (max_bin > half) max_bin = half;

    for (int b = 0; b < VIS_BARS; b++) {
        /* rango log de bin */
        float t0 = powf((float)b / VIS_BARS, 2.0f);
        float t1 = powf((float)(b + 1) / VIS_BARS, 2.0f);
        int bin0 = 1 + (int)(t0 * (max_bin - 1));
        int bin1 = 1 + (int)(t1 * (max_bin - 1));
        if (bin1 <= bin0) bin1 = bin0 + 1;
        if (bin1 > max_bin) bin1 = max_bin;

        float mag = 0.0f;
        int count = 0;
        for (int k = bin0; k < bin1; k++) {
            float re = vis->fft_real[k], im = vis->fft_imag[k];
            float m = sqrtf(re * re + im * im);
            if (m > mag) mag = m;
            count++;
        }
        (void)count;

        float norm = mag / (VIS_FFT_SIZE / 4.0f);
        float db = 20.0f * log10f(norm + 1e-4f);
        float level = (db + 40.0f) / 40.0f; /* -40dB..0dB -> 0..1 */
        if (level < 0) level = 0;
        if (level > 1) level = 1;

        /* Peak con caída suave para que no "tiemble" y se vea fluido */
        if (level > vis->bar_heights[b]) {
            vis->bar_heights[b] = level;
        } else {
            vis->bar_heights[b] -= 0.06f;
            if (vis->bar_heights[b] < level) vis->bar_heights[b] = level;
        }
    }
}

void visualizer_render(Visualizer *vis, SDL_Renderer *rend, SDL_Rect area) {
    if (!vis->enabled) return;

    int gap = 4;
    int bar_w = (area.w - gap * (VIS_BARS - 1)) / VIS_BARS;
    if (bar_w < 2) bar_w = 2;

    for (int b = 0; b < VIS_BARS; b++) {
        float h = vis->bar_heights[b];
        int bar_h = (int)(h * area.h);
        if (bar_h < 2) bar_h = 2;

        int x = area.x + b * (bar_w + gap);
        int y = area.y + area.h - bar_h;

        /* Gradiente rosa -> morado según la altura */
        float t = (float)b / (VIS_BARS - 1);
        Uint8 r = (Uint8)(VIS_COLOR_START_R + t * (VIS_COLOR_END_R - VIS_COLOR_START_R));
        Uint8 g = (Uint8)(VIS_COLOR_START_G + t * (VIS_COLOR_END_G - VIS_COLOR_START_G));
        Uint8 bl = (Uint8)(VIS_COLOR_START_B + t * (VIS_COLOR_END_B - VIS_COLOR_START_B));

        SDL_Rect bar = { x, y, bar_w, bar_h };
        SDL_SetRenderDrawColor(rend, r, g, bl, 255);
        SDL_RenderFillRect(rend, &bar);

        /* punta brillante */
        SDL_Rect tip = { x, y, bar_w, 3 };
        SDL_SetRenderDrawColor(rend, 255, 255, 255, 200);
        SDL_RenderFillRect(rend, &tip);
    }
}
