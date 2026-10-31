/*
 * hid_descriptor.h
 * -----------------
 * Descriptor de reporte HID (USB HID Report Descriptor) para un
 * "Gamepad" genérico. No es el descriptor real de un mando de Xbox:
 * eso usa el protocolo propietario GIP de Microsoft, que no se puede
 * emular por Bluetooth HID estándar. Este descriptor declara:
 *
 *   - 4 ejes de 8 bits con signo:  LX, LY, RX, RY   (-127..127)
 *   - 2 gatillos de 8 bits sin signo: LT, RT        (0..255)
 *   - 1 Hat Switch (D-Pad), 4 bits, valores 0..7 + "null" (sin pulsar)
 *   - 15 botones (1 bit cada uno) + 1 bit de relleno
 *
 * Total del "Input Report" (sin contar el byte de Report ID ni el
 * byte de cabecera 0xA1 que exige el canal L2CAP de HID): 9 bytes.
 *
 * Windows/Linux/Android lo reconocerán como un joystick HID genérico
 * (DirectInput en Windows), con 4 ejes, 2 ejes extra para gatillos,
 * un POV hat y 12 botones remapeables.
 */

#ifndef HID_DESCRIPTOR_H
#define HID_DESCRIPTOR_H

#include <stdint.h>
#include <stddef.h>

/* Report ID que usamos para el (único) reporte de entrada */
#define GAMEPAD_REPORT_ID   0x01

/* Tamaño de los datos del reporte, SIN el Report ID */
#define GAMEPAD_REPORT_DATA_LEN 9

static const uint8_t hid_report_descriptor[] = {
    0x05, 0x01,        /* Usage Page (Generic Desktop) */
    0x09, 0x05,        /* Usage (Gamepad) */
    0xA1, 0x01,        /* Collection (Application) */
    0x85, GAMEPAD_REPORT_ID, /*   Report ID (1) */

    /* --- Sticks: LX, LY, RX, RY (signed 8-bit) --- */
    0x05, 0x01,        /*   Usage Page (Generic Desktop) */
    0x09, 0x30,        /*   Usage (X)  -> Left Stick X */
    0x09, 0x31,        /*   Usage (Y)  -> Left Stick Y */
    0x09, 0x33,        /*   Usage (Rx) -> Right Stick X */
    0x09, 0x34,        /*   Usage (Ry) -> Right Stick Y */
    0x15, 0x81,        /*   Logical Minimum (-127) */
    0x25, 0x7F,        /*   Logical Maximum (127) */
    0x75, 0x08,        /*   Report Size (8) */
    0x95, 0x04,        /*   Report Count (4) */
    0x81, 0x02,        /*   Input (Data,Var,Abs) */

    /* --- Gatillos: LT, RT (unsigned 8-bit) --- */
    0x09, 0x32,        /*   Usage (Z)  -> Left Trigger */
    0x09, 0x35,        /*   Usage (Rz) -> Right Trigger */
    0x15, 0x00,        /*   Logical Minimum (0) */
    0x26, 0xFF, 0x00,  /*   Logical Maximum (255) */
    0x75, 0x08,        /*   Report Size (8) */
    0x95, 0x02,        /*   Report Count (2) */
    0x81, 0x02,        /*   Input (Data,Var,Abs) */

    /* --- D-Pad como Hat Switch --- */
    0x09, 0x39,        /*   Usage (Hat Switch) */
    0x15, 0x00,        /*   Logical Minimum (0) */
    0x25, 0x07,        /*   Logical Maximum (7) */
    0x35, 0x00,        /*   Physical Minimum (0) */
    0x46, 0x3B, 0x01,  /*   Physical Maximum (315) */
    0x65, 0x14,        /*   Unit (Eng Rot: Degrees) */
    0x75, 0x04,        /*   Report Size (4) */
    0x95, 0x01,        /*   Report Count (1) */
    0x81, 0x42,        /*   Input (Data,Var,Abs,Null) */
    0x65, 0x00,        /*   Unit (None) */
    0x75, 0x04,        /*   Report Size (4) -- relleno para alinear a byte */
    0x95, 0x01,        /*   Report Count (1) */
    0x81, 0x03,        /*   Input (Const,Var,Abs) */

    /* --- 15 botones (orden estandar de gamepad, ver enum abajo) --- */
    0x05, 0x09,        /*   Usage Page (Button) */
    0x19, 0x01,        /*   Usage Minimum (Button 1) */
    0x29, 0x0F,        /*   Usage Maximum (Button 15) */
    0x15, 0x00,        /*   Logical Minimum (0) */
    0x25, 0x01,        /*   Logical Maximum (1) */
    0x75, 0x01,        /*   Report Size (1) */
    0x95, 0x0F,        /*   Report Count (15) */
    0x81, 0x02,        /*   Input (Data,Var,Abs) */
    0x75, 0x01,        /*   Report Size (1) -- 1 bit de relleno (15+1 = 16 bits) */
    0x95, 0x01,        /*   Report Count (1) */
    0x81, 0x03,        /*   Input (Const,Var,Abs) */

    0xC0               /* End Collection */
};

#define HID_REPORT_DESCRIPTOR_LEN (sizeof(hid_report_descriptor))

/*
 * Bit del reporte = (numero de boton HID) - 1.
 *
 * Android y Linux (hid-input) asignan los botones de un gamepad HID por
 * posicion:  1=A 2=B 3=C 4=X 5=Y 6=Z 7=L1 8=R1 9=L2 10=R2 11=Select 12=Start
 * 13=Mode 14=L3 15=R3. Es el mismo orden (con huecos) que usa el mando Xbox
 * por Bluetooth. Los gatillos van como ejes, asi que los botones 3, 6, 9 y
 * 10 quedan sin usar.
 */
enum {
    GBTN_A       = 0,   /* boton 1  */
    GBTN_B       = 1,   /* boton 2  */
    GBTN_X       = 3,   /* boton 4  */
    GBTN_Y       = 4,   /* boton 5  */
    GBTN_LB      = 6,   /* boton 7  */
    GBTN_RB      = 7,   /* boton 8  */
    GBTN_BACK    = 10,  /* boton 11 */
    GBTN_START   = 11,  /* boton 12 */
    GBTN_GUIDE   = 12,  /* boton 13 */
    GBTN_LSTICK  = 13,  /* boton 14 */
    GBTN_RSTICK  = 14   /* boton 15 */
};

#endif /* HID_DESCRIPTOR_H */
