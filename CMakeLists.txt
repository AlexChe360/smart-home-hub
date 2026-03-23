cmake_minimum_required(VERSION 3.16)
project(smarthome_hub C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")

# Xcode / IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Platform detection ──
if(APPLE)
    message(STATUS "Building on macOS")

    if(EXISTS "/opt/homebrew")
        set(HOMEBREW_PREFIX "/opt/homebrew")
    else()
        set(HOMEBREW_PREFIX "/usr/local")
    endif()

    message(STATUS "Homebrew prefix: ${HOMEBREW_PREFIX}")

    set(ENV{PKG_CONFIG_PATH}
        "${HOMEBREW_PREFIX}/lib/pkgconfig:${HOMEBREW_PREFIX}/opt/openssl/lib/pkgconfig"
    )

    list(APPEND CMAKE_PREFIX_PATH
        "${HOMEBREW_PREFIX}"
        "${HOMEBREW_PREFIX}/opt/openssl"
    )

    include_directories("${HOMEBREW_PREFIX}/include")
    link_directories("${HOMEBREW_PREFIX}/lib")

    add_definitions(-Wno-deprecated-declarations)

    set(UUID_LIBS "")
    set(UUID_INCLUDE_DIRS "")
else()
    message(STATUS "Building on Linux")
    set(UUID_LIBS uuid)
endif()

# ── Find libraries ──
find_package(PkgConfig REQUIRED)

pkg_check_modules(MOSQUITTO REQUIRED libmosquitto)
pkg_check_modules(LWS REQUIRED libwebsockets)

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

if(NOT APPLE)
    pkg_check_modules(UUID REQUIRED uuid)
    set(UUID_LIBS ${UUID_LIBRARIES})
endif()

# ── Source files ──
set(SOURCES
    src/main.c
    src/config.c
    src/mqtt_client.c
    src/ws_client.c
    src/device_manager.c
    src/message.c
    src/hub_core.c
    src/logger.c
    src/cJSON.c
    src/provisioning.c
)

# ── Build ──
add_executable(smarthome-hub ${SOURCES})

target_include_directories(smarthome-hub PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${MOSQUITTO_INCLUDE_DIRS}
    ${LWS_INCLUDE_DIRS}
    ${UUID_INCLUDE_DIRS}
)

target_link_directories(smarthome-hub PRIVATE
    ${MOSQUITTO_LIBRARY_DIRS}
    ${LWS_LIBRARY_DIRS}
)

target_compile_options(smarthome-hub PRIVATE
    ${MOSQUITTO_CFLAGS_OTHER}
    ${LWS_CFLAGS_OTHER}
)

target_link_libraries(smarthome-hub PRIVATE
    ${MOSQUITTO_LIBRARIES}
    ${LWS_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl
    Threads::Threads
    ${UUID_LIBS}
)

# ── Install ──
install(TARGETS smarthome-hub DESTINATION /usr/local/bin)
install(FILES config/hub.conf.example DESTINATION /etc/smarthome/ RENAME hub.conf)
