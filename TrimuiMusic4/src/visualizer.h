#ifndef VISUALIZER_H
#define VISUALIZER_H

#include <SDL2/SDL.h>
#include <stdint.h>

#define VIS_FFT_SIZE 512   /* potencia de 2 */
#define VIS_BARS     32

typedef struct {
    float bar_heights[VIS_BARS];   /* suavizado con caída (peak-fall) */
    float fft_real[VIS_FFT_SIZE];
    float fft_imag[VIS_FFT_SIZE];
    int enabled;
} Visualizer;

void visualizer_init(Visualizer *vis);

/* samples: mono S16, al menos VIS_FFT_SIZE muestras */
void visualizer_update(Visualizer *vis, const int16_t *samples, int n_samples);

void visualizer_render(Visualizer *vis, SDL_Renderer *rend, SDL_Rect area);

#endif
