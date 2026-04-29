#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <ViGEm/Client.h>
#include <atomic>

#pragma pack(push, 1)
struct GamepadState {
    uint16_t buttons;
    int16_t leftX;
    int16_t leftY;
    int16_t rightX;
    int16_t rightY;
};
#pragma pack(pop)

// IDs de botones en ViGEm (XINPUT)
#define BTN_A       (1 << 0)
#define BTN_B       (1 << 1)
#define BTN_X       (1 << 2)
#define BTN_Y       (1 << 3)
#define BTN_LB      (1 << 4)
#define BTN_RB      (1 << 5)
#define BTN_BACK    (1 << 6)
#define BTN_START   (1 << 7)
#define BTN_GUIDE   (1 << 8)
#define BTN_LSTICK  (1 << 9)
#define BTN_RSTICK  (1 << 10)
#define BTN_DPAD_U  (1 << 11)
#define BTN_DPAD_D  (1 << 12)
#define BTN_DPAD_L  (1 << 13)
#define BTN_DPAD_R  (1 << 14)

void applyState(PVIGEM_TARGET target, const GamepadState& state) {
    XUSB_REPORT report;
    ZeroMemory(&report, sizeof(report));

    // Mapeo de botones
    if (state.buttons & BTN_A)      report.wButtons |= XUSB_GAMEPAD_A;
    if (state.buttons & BTN_B)      report.wButtons |= XUSB_GAMEPAD_B;
    if (state.buttons & BTN_X)      report.wButtons |= XUSB_GAMEPAD_X;
    if (state.buttons & BTN_Y)      report.wButtons |= XUSB_GAMEPAD_Y;
    if (state.buttons & BTN_LB)     report.wButtons |= XUSB_GAMEPAD_LEFT_SHOULDER;
    if (state.buttons & BTN_RB)     report.wButtons |= XUSB_GAMEPAD_RIGHT_SHOULDER;
    if (state.buttons & BTN_BACK)   report.wButtons |= XUSB_GAMEPAD_BACK;
    if (state.buttons & BTN_START)  report.wButtons |= XUSB_GAMEPAD_START;
    if (state.buttons & BTN_GUIDE)  report.wButtons |= XUSB_GAMEPAD_GUIDE;
    if (state.buttons & BTN_LSTICK) report.wButtons |= XUSB_GAMEPAD_LEFT_THUMB;
    if (state.buttons & BTN_RSTICK) report.wButtons |= XUSB_GAMEPAD_RIGHT_THUMB;
    if (state.buttons & BTN_DPAD_U) report.wButtons |= XUSB_GAMEPAD_DPAD_UP;
    if (state.buttons & BTN_DPAD_D) report.wButtons |= XUSB_GAMEPAD_DPAD_DOWN;
    if (state.buttons & BTN_DPAD_L) report.wButtons |= XUSB_GAMEPAD_DPAD_LEFT;
    if (state.buttons & BTN_DPAD_R) report.wButtons |= XUSB_GAMEPAD_DPAD_RIGHT;

    // Ejes: SDL devuelve valores entre -32768 y 32767, ViGEm espera SHORT
    report.sThumbLX = state.leftX;
    report.sThumbLY = state.leftY;
    report.sThumbRX = state.rightX;
    report.sThumbRY = state.rightY;

    vigem_target_x360_update(VIGEM_CLIENT_SINGLETON, target, report);
}

int main() {
    // --- Inicializar Winsock ---
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    // --- Crear socket UDP ---
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "socket failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(8888); // puerto del cliente

    if (bind(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        fprintf(stderr, "bind failed: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    // --- Inicializar ViGEm ---
    VIGEM_CLIENT_T client = vigem_alloc();
    if (!VIGEM_SUCCESS(vigem_connect(client))) {
        fprintf(stderr, "No se pudo conectar al bus ViGEm. ¿Instalaste ViGEmBus?\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    // Crear un mando Xbox 360 virtual
    VIGEM_TARGET_T target = vigem_target_x360_alloc();
    if (!VIGEM_SUCCESS(vigem_target_add(client, target))) {
        fprintf(stderr, "No se pudo añadir el dispositivo virtual.\n");
        vigem_disconnect(client);
        vigem_free(client);
        closesocket(sock);
        WSACleanup();
        return 1;
    }
    printf("Servidor listo. Esperando datos en puerto 8888...\n");

    // --- Bucle de recepción ---
    GamepadState state;
    sockaddr_in clientAddr;
    int clientAddrSize = sizeof(clientAddr);

    while (true) {
        int bytes = recvfrom(sock, (char*)&state, sizeof(state), 0,
            (sockaddr*)&clientAddr, &clientAddrSize);
        if (bytes == sizeof(state)) {
            applyState(target, state);
        }
        else if (bytes == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) {
                fprintf(stderr, "Error en recvfrom: %d\n", err);
                break;
            }
        }
        // Pequeño descanso para no saturar CPU
        Sleep(1);
    }

    // --- Limpieza ---
    vigem_target_remove(client, target);
    vigem_target_free(target);
    vigem_disconnect(client);
    vigem_free(client);
    closesocket(sock);
    WSACleanup();
    return 0;
}