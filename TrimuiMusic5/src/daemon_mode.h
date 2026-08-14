#ifndef DAEMON_MODE_H
#define DAEMON_MODE_H

#define DAEMON_PID_FILE "/tmp/trimuimusic.pid"

/* Punto de entrada cuando el binario se relanza a si mismo como daemon de
 * fondo (argv: --daemon <carpeta> <archivo> <pos_segundos> <pausado 0|1>). */
int run_headless_daemon(const char *folder, const char *file, double start_pos, int start_paused);

/* Intenta poner la reproduccion actual en 2do plano: hace fork + re-exec de
 * una copia headless de este mismo binario que sigue reproduciendo de forma
 * independiente (sin ventana/SDL video), y devuelve inmediatamente. El
 * proceso que llama debe seguir su cierre normal despues de esto.
 * Devuelve 1 si se pudo lanzar el daemon, 0 si no (p.ej. no se encontro el
 * propio ejecutable en /proc/self/exe). */
int spawn_background_daemon(const char *folder, const char *file, double pos, int paused);

/* Devuelve el PID de un daemon de fondo ya corriendo (leido de
 * DAEMON_PID_FILE y verificado con kill(pid,0)), o -1 si no hay ninguno vivo. */
long detect_running_daemon(void);

/* Envia SIGTERM al daemon de fondo indicado para detener la musica. */
void stop_background_daemon(long pid);

#endif
