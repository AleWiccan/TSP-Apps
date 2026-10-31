/*
 * bt_hid.c
 * --------
 * Ver bt_hid.h para la explicación general. Aquí solo se implementa:
 *   1) El registro del Service Record HID en el sdpd local (para que
 *      un PC que haga "Add Bluetooth device" nos descubra como HID).
 *   2) Los sockets L2CAP en bruto (PSM 0x11 control, 0x13 interrupción)
 *      donde el host se conecta una vez emparejado.
 *   3) El envío de reportes de entrada y una respuesta mínima a las
 *      transacciones más comunes del canal de control.
 *
 * Todo lo relativo a nombre del adaptador / clase de dispositivo /
 * discoverable-pairable / agente de emparejamiento se gestiona FUERA
 * de este binario, desde package/bt-setup.sh, con `bluetoothctl` y
 * `hciconfig`/`btmgmt` — son comandos estables entre versiones de
 * BlueZ, mientras que las firmas de las funciones hci_lib.h para eso
 * cambian bastante de una versión a otra.
 */

#include "bt_hid.h"
#include "hid_descriptor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <poll.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>

#define PSM_HID_CTRL 0x11
#define PSM_HID_INTR 0x13

/* Cabeceras de transacción HID-over-L2CAP (ver Bluetooth HID Profile spec) */
#define HIDP_TRANS_HANDSHAKE   0x00
#define HIDP_TRANS_SET_PROTOCOL 0x70
#define HIDP_TRANS_GET_REPORT  0x40
#define HIDP_TRANS_DATA        0xA0
#define HIDP_DATA_RTYPE_INPUT  0x01
#define HIDP_HSHK_SUCCESSFUL   0x00

typedef enum {
    BT_STATE_LISTENING = 0,
    BT_STATE_WAIT_INTR,
    BT_STATE_CONNECTED
} bt_state_t;

struct bt_hid_ctx {
    sdp_session_t *sdp_session;
    uint32_t       sdp_handle;

    int ctrl_listen;
    int intr_listen;
    int ctrl_conn;
    int intr_conn;

    bt_state_t state;
    char peer_addr[18];

    uint8_t last_report[GAMEPAD_REPORT_DATA_LEN];
};

/* ---------- SDP ---------- */

static void add_lang_attr(sdp_record_t *r)
{
    sdp_lang_attr_t base_lang;
    sdp_list_t *langs = NULL;
    base_lang.code_ISO639 = (0x65 << 8) | 0x6e; /* "en" */
    base_lang.encoding = 106;                   /* UTF-8 */
    base_lang.base_offset = SDP_PRIMARY_LANG_BASE;
    langs = sdp_list_append(NULL, &base_lang);
    sdp_set_lang_attr(r, langs);
    sdp_list_free(langs, NULL);
}

/* Construye el Service Record HID completo en `record` (sin tocar la red). */
static int build_hid_record(sdp_record_t *record, const char *service_name)
{
    uuid_t root_uuid, hid_uuid, l2cap_uuid, hidp_uuid;
    sdp_profile_desc_t profile[1];
    sdp_list_t *svclass_id, *pfseq, *root, *aproto, *apseq;
    sdp_list_t *proto_l2cap, *proto_hidp;
    sdp_data_t *psm_ctrl, *psm_intr;
    uint16_t u_ctrl = PSM_HID_CTRL, u_intr = PSM_HID_INTR;
    uint16_t release_num = 0x0100, parser_ver = 0x0111, profile_ver = 0x0100;
    uint16_t supervision_timeout = 0x0C80;
    uint8_t  device_subclass = 0x08;   /* Peripheral: Gamepad */
    uint8_t  country_code = 0x00;
    uint8_t  yes = 1, no = 0;

    memset(record, 0, sizeof(*record));
    record->handle = 0xffffffff;

    sdp_uuid16_create(&root_uuid, PUBLIC_BROWSE_GROUP);
    root = sdp_list_append(NULL, &root_uuid);
    sdp_set_browse_groups(record, root);
    sdp_list_free(root, NULL);

    add_lang_attr(record);

    sdp_uuid16_create(&hid_uuid, HID_SVCLASS_ID);
    svclass_id = sdp_list_append(NULL, &hid_uuid);
    sdp_set_service_classes(record, svclass_id);
    sdp_list_free(svclass_id, NULL);

    sdp_uuid16_create(&profile[0].uuid, HID_PROFILE_ID);
    profile[0].version = profile_ver;
    pfseq = sdp_list_append(NULL, &profile[0]);
    sdp_set_profile_descs(record, pfseq);
    sdp_list_free(pfseq, NULL);

    /* Protocolo principal: L2CAP (PSM control) + HIDP */
    sdp_uuid16_create(&l2cap_uuid, L2CAP_UUID);
    sdp_uuid16_create(&hidp_uuid, HIDP_UUID);
    proto_l2cap = sdp_list_append(NULL, &l2cap_uuid);
    psm_ctrl = sdp_data_alloc(SDP_UINT16, &u_ctrl);
    proto_l2cap = sdp_list_append(proto_l2cap, psm_ctrl);
    apseq = sdp_list_append(NULL, proto_l2cap);
    proto_hidp = sdp_list_append(NULL, &hidp_uuid);
    apseq = sdp_list_append(apseq, proto_hidp);
    aproto = sdp_list_append(NULL, apseq);
    sdp_set_access_protos(record, aproto);
    sdp_list_free(proto_l2cap, NULL);
    sdp_list_free(proto_hidp, NULL);
    sdp_list_free(apseq, NULL);
    sdp_list_free(aproto, NULL);
    sdp_data_free(psm_ctrl);

    /* Protocolo adicional: L2CAP (PSM interrupcion) + HIDP */
    proto_l2cap = sdp_list_append(NULL, &l2cap_uuid);
    psm_intr = sdp_data_alloc(SDP_UINT16, &u_intr);
    proto_l2cap = sdp_list_append(proto_l2cap, psm_intr);
    apseq = sdp_list_append(NULL, proto_l2cap);
    proto_hidp = sdp_list_append(NULL, &hidp_uuid);
    apseq = sdp_list_append(apseq, proto_hidp);
    aproto = sdp_list_append(NULL, apseq);
    sdp_set_add_access_protos(record, aproto);
    sdp_list_free(proto_l2cap, NULL);
    sdp_list_free(proto_hidp, NULL);
    sdp_list_free(apseq, NULL);
    sdp_list_free(aproto, NULL);
    sdp_data_free(psm_intr);

    sdp_set_info_attr(record, service_name, "TrimuiXboxPad", "Trimui Smart Pro gamepad");

    sdp_attr_add_new(record, SDP_ATTR_HID_DEVICE_RELEASE_NUMBER, SDP_UINT16, &release_num);
    sdp_attr_add_new(record, SDP_ATTR_HID_PARSER_VERSION, SDP_UINT16, &parser_ver);
    sdp_attr_add_new(record, SDP_ATTR_HID_DEVICE_SUBCLASS, SDP_UINT8, &device_subclass);
    sdp_attr_add_new(record, SDP_ATTR_HID_COUNTRY_CODE, SDP_UINT8, &country_code);
    sdp_attr_add_new(record, SDP_ATTR_HID_VIRTUAL_CABLE, SDP_BOOL, &yes);
    sdp_attr_add_new(record, SDP_ATTR_HID_RECONNECT_INITIATE, SDP_BOOL, &yes);

    /* HIDDescriptorList = SEQ{ SEQ{ uint8 0x22, text-string(descriptor) } } */
    {
        uint8_t desc_type = 0x22;
        sdp_data_t *d_type = sdp_data_alloc(SDP_UINT8, &desc_type);
        sdp_data_t *d_desc = sdp_data_alloc_with_length(SDP_TEXT_STR8, hid_report_descriptor,
                                                        (uint32_t)HID_REPORT_DESCRIPTOR_LEN);
        if (!d_type || !d_desc) return -1;
        d_type->next = d_desc;
        sdp_data_t *inner = sdp_data_alloc(SDP_SEQ8, d_type);
        sdp_data_t *outer = sdp_data_alloc(SDP_SEQ8, inner);
        if (!inner || !outer) return -1;
        sdp_attr_add(record, SDP_ATTR_HID_DESCRIPTOR_LIST, outer);
    }

    /* HIDLANGIDBaseList = SEQ{ SEQ{ uint16 0x0409, uint16 0x0100 } } */
    {
        uint16_t lang_id = 0x0409, base = 0x0100;
        sdp_data_t *l1 = sdp_data_alloc(SDP_UINT16, &lang_id);
        sdp_data_t *l2 = sdp_data_alloc(SDP_UINT16, &base);
        if (!l1 || !l2) return -1;
        l1->next = l2;
        sdp_data_t *inner = sdp_data_alloc(SDP_SEQ8, l1);
        sdp_data_t *outer = sdp_data_alloc(SDP_SEQ8, inner);
        if (!inner || !outer) return -1;
        sdp_attr_add(record, SDP_ATTR_HID_LANG_ID_BASE_LIST, outer);
    }

    sdp_attr_add_new(record, SDP_ATTR_HID_BATTERY_POWER, SDP_BOOL, &yes);
    sdp_attr_add_new(record, SDP_ATTR_HID_REMOTE_WAKEUP, SDP_BOOL, &yes);
    sdp_attr_add_new(record, SDP_ATTR_HID_PROFILE_VERSION, SDP_UINT16, &profile_ver);
    sdp_attr_add_new(record, SDP_ATTR_HID_SUPERVISION_TIMEOUT, SDP_UINT16, &supervision_timeout);
    sdp_attr_add_new(record, SDP_ATTR_HID_NORMALLY_CONNECTABLE, SDP_BOOL, &yes);
    sdp_attr_add_new(record, SDP_ATTR_HID_BOOT_DEVICE, SDP_BOOL, &no);
    return 0;
}

static int register_hid_sdp(const char *service_name, sdp_session_t **out_session,
                             uint32_t *out_handle)
{
    sdp_session_t *session;
    static sdp_record_t record;

    fprintf(stderr, "[bt_hid] construyendo Service Record HID...\n");
    if (build_hid_record(&record, service_name) != 0) {
        fprintf(stderr, "[bt_hid] ERROR construyendo el Service Record\n");
        return -1;
    }

    fprintf(stderr, "[bt_hid] conectando al sdpd local (/var/run/sdp)...\n");
    session = sdp_connect(BDADDR_ANY, BDADDR_LOCAL, SDP_RETRY_IF_BUSY);
    if (!session) {
        fprintf(stderr, "[bt_hid] No se pudo conectar al sdpd local: %s\n"
                        "         bluetoothd debe arrancar con --compat (ver bt-setup.sh).\n",
                strerror(errno));
        return -1;
    }

    fprintf(stderr, "[bt_hid] registrando el record en sdpd...\n");
    if (sdp_record_register(session, &record, 0) < 0) {
        fprintf(stderr, "[bt_hid] Fallo al registrar el Service Record HID: %s\n", strerror(errno));
        sdp_close(session);
        return -1;
    }
    fprintf(stderr, "[bt_hid] record registrado, handle 0x%x\n", record.handle);

    *out_session = session;
    *out_handle = record.handle;
    return 0;
}

/* ---------- L2CAP ---------- */

static int l2cap_listen_on_psm(uint16_t psm)
{
    int sock;
    struct sockaddr_l2 addr;

    sock = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    if (sock < 0) {
        fprintf(stderr, "[bt_hid] socket(L2CAP) fallo: %s\n", strerror(errno));
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.l2_family = AF_BLUETOOTH;
    bacpy(&addr.l2_bdaddr, BDADDR_ANY);
    addr.l2_psm = htobs(psm);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[bt_hid] bind PSM 0x%02x fallo: %s\n"
                        "         (Necesitas root, y que bluetoothd NO tenga\n"
                        "          ocupado este PSM -- revisa el plugin 'input'\n"
                        "          en /etc/bluetooth/main.conf, ver README)\n",
                psm, strerror(errno));
        close(sock);
        return -1;
    }

    if (listen(sock, 1) < 0) {
        fprintf(stderr, "[bt_hid] listen PSM 0x%02x fallo: %s\n", psm, strerror(errno));
        close(sock);
        return -1;
    }

    /* No bloqueante: bt_hid_poll() se llama una vez por frame */
    fcntl(sock, F_SETFL, O_NONBLOCK);

    return sock;
}

static int try_accept(int listen_sock, char *peer_out)
{
    struct sockaddr_l2 addr;
    socklen_t len = sizeof(addr);
    int fd = accept(listen_sock, (struct sockaddr *)&addr, &len);
    if (fd < 0)
        return -1;
    if (peer_out) {
        char buf[18];
        ba2str(&addr.l2_bdaddr, buf);
        snprintf(peer_out, 18, "%s", buf);
    }
    return fd;
}

/* ---------- API pública ---------- */

bt_hid_ctx_t *bt_hid_start(const char *device_name)
{
    bt_hid_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    /* calloc deja 0, que es un fd valido (stdin): inicializar a -1 */
    ctx->ctrl_listen = ctx->intr_listen = -1;
    ctx->ctrl_conn = ctx->intr_conn = -1;

    if (register_hid_sdp(device_name ? device_name : "Trimui Gamepad",
                          &ctx->sdp_session, &ctx->sdp_handle) != 0) {
        free(ctx);
        return NULL;
    }

    ctx->ctrl_listen = l2cap_listen_on_psm(PSM_HID_CTRL);
    ctx->intr_listen = l2cap_listen_on_psm(PSM_HID_INTR);
    if (ctx->ctrl_listen < 0 || ctx->intr_listen < 0) {
        bt_hid_stop(ctx);
        return NULL;
    }

    ctx->ctrl_conn = -1;
    ctx->intr_conn = -1;
    ctx->state = BT_STATE_LISTENING;

    printf("[bt_hid] Escuchando en PSM 0x11 (control) y 0x13 (interrupcion).\n"
           "         Empareja el PC con este dispositivo desde su panel de\n"
           "         Bluetooth para conectar.\n");

    return ctx;
}

static void handle_ctrl_channel(bt_hid_ctx_t *ctx)
{
    uint8_t buf[64];
    ssize_t n = recv(ctx->ctrl_conn, buf, sizeof(buf), MSG_DONTWAIT);
    if (n <= 0) {
        if (n == 0) {
            /* Host cerro el canal de control -> se desconecta todo */
            close(ctx->ctrl_conn); ctx->ctrl_conn = -1;
            close(ctx->intr_conn); ctx->intr_conn = -1;
            ctx->state = BT_STATE_LISTENING;
        }
        return;
    }

    uint8_t hdr = buf[0] & 0xf0;

    if (hdr == HIDP_TRANS_SET_PROTOCOL) {
        uint8_t reply = HIDP_TRANS_HANDSHAKE | HIDP_HSHK_SUCCESSFUL;
        send(ctx->ctrl_conn, &reply, 1, MSG_NOSIGNAL);
    } else if (hdr == HIDP_TRANS_GET_REPORT) {
        uint8_t reply[2 + GAMEPAD_REPORT_DATA_LEN];
        reply[0] = 0xA1; /* DATA | Input report */
        reply[1] = GAMEPAD_REPORT_ID;
        memcpy(reply + 2, ctx->last_report, GAMEPAD_REPORT_DATA_LEN);
        send(ctx->ctrl_conn, reply, sizeof(reply), MSG_NOSIGNAL);
    } else {
        /* Cualquier otra transaccion: confirmamos por si el host espera ACK */
        uint8_t reply = HIDP_TRANS_HANDSHAKE | HIDP_HSHK_SUCCESSFUL;
        send(ctx->ctrl_conn, &reply, 1, MSG_NOSIGNAL);
    }
}

int bt_hid_poll(bt_hid_ctx_t *ctx)
{
    if (!ctx) return -1;

    switch (ctx->state) {
    case BT_STATE_LISTENING: {
        int fd = try_accept(ctx->ctrl_listen, ctx->peer_addr);
        if (fd >= 0) {
            ctx->ctrl_conn = fd;
            ctx->state = BT_STATE_WAIT_INTR;
            printf("[bt_hid] Canal de control conectado desde %s, esperando "
                   "canal de interrupcion...\n", ctx->peer_addr);
        }
        return 0;
    }
    case BT_STATE_WAIT_INTR: {
        int fd = try_accept(ctx->intr_listen, NULL);
        if (fd >= 0) {
            ctx->intr_conn = fd;
            ctx->state = BT_STATE_CONNECTED;
            printf("[bt_hid] Canal de interrupcion conectado. Host %s listo.\n",
                   ctx->peer_addr);
            return 1;
        }
        /* Si tarda demasiado o el ctrl se cerro, procesa por si acaso */
        {
            struct pollfd pfd = { .fd = ctx->ctrl_conn, .events = POLLIN };
            if (poll(&pfd, 1, 0) > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
                handle_ctrl_channel(ctx);
            }
        }
        return 0;
    }
    case BT_STATE_CONNECTED: {
        struct pollfd pfds[2];
        pfds[0].fd = ctx->ctrl_conn; pfds[0].events = POLLIN;
        pfds[1].fd = ctx->intr_conn; pfds[1].events = POLLIN;
        int r = poll(pfds, 2, 0);
        if (r > 0) {
            if (pfds[0].revents & (POLLIN | POLLHUP | POLLERR))
                handle_ctrl_channel(ctx);
            if (pfds[1].revents & (POLLHUP | POLLERR)) {
                close(ctx->ctrl_conn); ctx->ctrl_conn = -1;
                close(ctx->intr_conn); ctx->intr_conn = -1;
                ctx->state = BT_STATE_LISTENING;
                printf("[bt_hid] Host desconectado.\n");
                return -1;
            }
        }
        return 1;
    }
    }
    return 0;
}

int bt_hid_is_connected(bt_hid_ctx_t *ctx)
{
    return ctx && ctx->state == BT_STATE_CONNECTED;
}

const char *bt_hid_peer_address(bt_hid_ctx_t *ctx)
{
    return ctx ? ctx->peer_addr : "";
}

int bt_hid_send_report(bt_hid_ctx_t *ctx, const uint8_t *data, size_t len)
{
    if (!ctx || ctx->state != BT_STATE_CONNECTED) return -1;
    if (len != GAMEPAD_REPORT_DATA_LEN) return -1;

    uint8_t frame[2 + GAMEPAD_REPORT_DATA_LEN];
    frame[0] = 0xA1; /* DATA | Input report */
    frame[1] = GAMEPAD_REPORT_ID;
    memcpy(frame + 2, data, GAMEPAD_REPORT_DATA_LEN);
    memcpy(ctx->last_report, data, GAMEPAD_REPORT_DATA_LEN);

    ssize_t n = send(ctx->intr_conn, frame, sizeof(frame), MSG_NOSIGNAL);
    if (n < 0) {
        close(ctx->ctrl_conn); ctx->ctrl_conn = -1;
        close(ctx->intr_conn); ctx->intr_conn = -1;
        ctx->state = BT_STATE_LISTENING;
        return -1;
    }
    return 0;
}

void bt_hid_stop(bt_hid_ctx_t *ctx)
{
    if (!ctx) return;

    if (ctx->ctrl_conn >= 0) close(ctx->ctrl_conn);
    if (ctx->intr_conn >= 0) close(ctx->intr_conn);
    if (ctx->ctrl_listen >= 0) close(ctx->ctrl_listen);
    if (ctx->intr_listen >= 0) close(ctx->intr_listen);

    if (ctx->sdp_session) {
        /* OJO: sdp_record_unregister()/sdp_device_record_unregister() liberan
         * el sdp_record_t (aqui es estatico) -> "double free". Usar la
         * variante binaria, que solo manda el handle. */
        sdp_device_record_unregister_binary(ctx->sdp_session, BDADDR_ANY, ctx->sdp_handle);
        sdp_close(ctx->sdp_session);
    }

    free(ctx);
}
