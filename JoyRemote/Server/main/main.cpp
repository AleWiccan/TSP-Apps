#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <cstdint>
#include <ViGEm/Client.h>
#include <vector>
#include <string>

#pragma pack(push, 1)
struct GamepadState {
    uint16_t buttons;
    int16_t leftX, leftY, rightX, rightY;
    int16_t leftTrigger, rightTrigger;
};
#pragma pack(pop)

// IDs de botones
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

void applyState(PVIGEM_CLIENT client, PVIGEM_TARGET target, const GamepadState& state) {
    XUSB_REPORT report{};

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

    report.sThumbLX = state.leftX;
    report.sThumbLY = state.leftY;
    report.sThumbRX = state.rightX;
    report.sThumbRY = state.rightY;
    report.bLeftTrigger = static_cast<uint8_t>(state.leftTrigger >> 8);
    report.bRightTrigger = static_cast<uint8_t>(state.rightTrigger >> 8);

    vigem_target_x360_update(client, target, report);
}

// Imprime las direcciones IP locales (IPv4, no loopback)
void PrintLocalIPs() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        struct addrinfo hints = {}, * result = nullptr;
        hints.ai_family = AF_INET;      // Solo IPv4
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        if (getaddrinfo(hostname, nullptr, &hints, &result) == 0) {
            std::vector<std::string> ips;
            for (struct addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
                struct sockaddr_in* addr = (struct sockaddr_in*)ptr->ai_addr;
                char ipStr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &addr->sin_addr, ipStr, sizeof(ipStr));
                // Excluir loopback
                if (strcmp(ipStr, "127.0.0.1") != 0) {
                    ips.push_back(ipStr);
                }
            }
            freeaddrinfo(result);

            if (!ips.empty()) {
                printf("\nDirecciones IP disponibles para conectar la consola:\n");
                for (const auto& ip : ips) {
                    printf("  - %s\n", ip.c_str());
                }
                printf("\n");
            }
            else {
                printf("No se encontraron direcciones IPv4 (no loopback). Verifica la red.\n");
            }
        }
        else {
            printf("Error al obtener información de red.\n");
        }
    }
    else {
        printf("Error al obtener el nombre del host.\n");
    }
}

int main() {
    // Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "socket failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(8888);

    if (bind(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        fprintf(stderr, "bind failed: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    // ViGEm
    PVIGEM_CLIENT client = vigem_alloc();
    if (client == nullptr) {
        fprintf(stderr, "No se pudo asignar el cliente ViGEm.\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    if (!VIGEM_SUCCESS(vigem_connect(client))) {
        fprintf(stderr, "No se pudo conectar al bus ViGEm. ¿Instalaste ViGEmBus?\n");
        vigem_free(client);
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    PVIGEM_TARGET target = vigem_target_x360_alloc();
    if (target == nullptr) {
        fprintf(stderr, "No se pudo asignar el target del mando virtual.\n");
        vigem_disconnect(client);
        vigem_free(client);
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    if (!VIGEM_SUCCESS(vigem_target_add(client, target))) {
        fprintf(stderr, "No se pudo añadir el dispositivo virtual.\n");
        vigem_target_free(target);
        vigem_disconnect(client);
        vigem_free(client);
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    printf("Servidor listo. Esperando datos en puerto 8888...\n");

    // Mostrar las IPs disponibles
    PrintLocalIPs();

    GamepadState state;
    sockaddr_in clientAddr;
    int clientAddrSize = sizeof(clientAddr);

    while (true) {
        int bytes = recvfrom(sock, (char*)&state, sizeof(state), 0,
            (sockaddr*)&clientAddr, &clientAddrSize);
        if (bytes == sizeof(state)) {
            applyState(client, target, state);
        }
        else if (bytes == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) {
                fprintf(stderr, "Error en recvfrom: %d\n", err);
                break;
            }
        }
        Sleep(1);
    }

    vigem_target_remove(client, target);
    vigem_target_free(target);
    vigem_disconnect(client);
    vigem_free(client);
    closesocket(sock);
    WSACleanup();
    return 0;
}