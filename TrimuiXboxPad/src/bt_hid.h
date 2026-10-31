/*
 * bt_hid.h
 * --------
 * Emulación de un dispositivo Bluetooth HID (rol "device"/periférico),
 * clásico BR/EDR, usando los sockets L2CAP en bruto de BlueZ + registro
 * manual del Service Record en el SDP local (sdpd).
 *
 * Esto es lo mismo que hacía la vieja herramienta `hidd --server` de
 * bluez-utils, retirada de BlueZ hace años. No depende de D-Bus.
 *
 * Requiere: bluetoothd corriendo en el dispositivo, con el plugin
 * "input" desactivado (ver README) para no chocar por los PSM 0x11/0x13,
 * y el proceso debe correr como root (los sockets L2CAP con PSM < 0x1000
 * requieren privilegios).
 */

#ifndef BT_HID_H
#define BT_HID_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bt_hid_ctx bt_hid_ctx_t;

/*
 * Arranca todo: registra el Service Record HID en el sdpd local y deja
 * los sockets L2CAP de control (PSM 0x11) e interrupción (PSM 0x13) en
 * modo escucha (listen). Configura además el adaptador local (nombre,
 * clase de dispositivo, discoverable+pairable).
 *
 * device_name: nombre Bluetooth que verán los PCs al escanear.
 * Devuelve un contexto (o NULL si algo falló; revisa stderr).
 */
bt_hid_ctx_t *bt_hid_start(const char *device_name);

/*
 * Debe llamarse periódicamente (p.ej. una vez por frame) desde el hilo
 * principal. Si hay una conexión entrante pendiente la acepta (no
 * bloquea más de unos milisegundos), y si ya hay un host conectado,
 * atiende peticiones simples del canal de control (SET_PROTOCOL,
 * GET_REPORT) para llevarse mejor con hosts estrictos como Windows.
 *
 * Devuelve 1 si hay un host conectado y listo para recibir reportes,
 * 0 si no hay conexión todavía, -1 si el host se desconectó en esta
 * llamada (para que la UI pueda avisar).
 */
int bt_hid_poll(bt_hid_ctx_t *ctx);

/* Devuelve 1 si hay un host conectado ahora mismo */
int bt_hid_is_connected(bt_hid_ctx_t *ctx);

/* Dirección MAC (texto, "AA:BB:CC:DD:EE:FF") del host conectado, o "" */
const char *bt_hid_peer_address(bt_hid_ctx_t *ctx);

/*
 * Envía un Input Report por el canal de interrupción. `data` debe medir
 * exactamente GAMEPAD_REPORT_DATA_LEN bytes (ver hid_descriptor.h);
 * la función añade por su cuenta la cabecera 0xA1 y el Report ID.
 * Devuelve 0 en éxito, -1 si falló el envío (p.ej. el host se
 * desconectó justo ahora).
 */
int bt_hid_send_report(bt_hid_ctx_t *ctx, const uint8_t *data, size_t len);

/* Libera todo: desregistra el SDP record, cierra sockets */
void bt_hid_stop(bt_hid_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* BT_HID_H */
