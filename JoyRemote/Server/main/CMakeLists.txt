cmake_minimum_required(VERSION 3.10)
project(ServerUDP_ViGEm)

# --- Rutas de ViGEmClient (ajústalas a tu sistema) ---
set(VIGEM_ROOT "D:/Juegos/MicroTrimui/Apps/AleWiccan/TSP Apps/JoyRemote/Server/main/ViGEmClient/")                 # Carpeta base
set(VIGEM_INCLUDE_DIR "${VIGEM_ROOT}/include")   # Headers
set(VIGEM_LIB_DIR "${VIGEM_ROOT}/lib")           # .lib

# --- Ejecutable ---
add_executable(server main.cpp)

# --- Incluir los headers de ViGEm ---
target_include_directories(server PRIVATE ${VIGEM_INCLUDE_DIR})

# --- Buscar la biblioteca .lib y enlazarla ---
target_link_directories(server PRIVATE ${VIGEM_LIB_DIR})

# Enlazar ViGEmClient.lib y otras dependencias requeridas por ViGEmClient
target_link_libraries(server
    ViGEmClient.lib
    setupapi.lib        # Necesaria para funciones de instalación de dispositivos
    ws2_32             # Winsock2 para UDP
)