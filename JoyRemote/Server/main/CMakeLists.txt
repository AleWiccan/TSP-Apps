cmake_minimum_required(VERSION 3.10)
project(ServerUDP_ViGEm)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")   # /MD

# ... resto de configuraciones (rutas a ViGEmClient)
set(VIGEM_ROOT "D:/Juegos/MicroTrimui/Apps/AleWiccan/TSP Apps/JoyRemote/Server/main/ViGEmClient/")
set(VIGEM_INCLUDE_DIR "${VIGEM_ROOT}/include")
set(VIGEM_LIB_DIR "${VIGEM_ROOT}/lib")

add_executable(server main.cpp)
target_include_directories(server PRIVATE ${VIGEM_INCLUDE_DIR})
target_link_directories(server PRIVATE ${VIGEM_LIB_DIR})
target_link_libraries(server ViGEmClient.lib setupapi.lib ws2_32)