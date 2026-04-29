cmake_minimum_required(VERSION 3.10)
project(ServerUDP_ViGEm)

find_package(ViGEmClient REQUIRED)   # Si usas vcpkg, esto funciona

add_executable(server main.cpp)
target_link_libraries(server PRIVATE ViGEmClient::ViGEmClient ws2_32)