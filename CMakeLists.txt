# CMakeLists.txt
#
# Copyright (c) 2024-2025 Omar Berrow

cmake_minimum_required(VERSION 3.10)

set (CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/out/")

option (OBOS_STRAP_ENABLE_GIT "Add git repository support for sources." ON)
option (OBOS_STRAP_MLIBC "Whether mlibc is the target libc" OFF)

project (
    obos-strap
    LANGUAGES C
    DESCRIPTION "Yet another build tool for operating systems"
    HOMEPAGE_URL https://github.com/oberrow/obos-strap
)

set_property(GLOBAL PROPERTY C_STANDARD 17
                    PROPERTY C_STANDARD_REQUIRED true
                    PROPERTY C_EXTENSIONS On)

find_package(cJSON REQUIRED)

find_package(CURL)

if (NOT UNIX)
    message(FATAL_ERROR "obos-strap only supports unix-based systems")
endif()

if (${CURL_FOUND})
    add_compile_definitions(HAS_LIBCURL=1)
    set(HAS_CURL 1)
else()
    set(HAS_CURL 0)
endif()

if (${OBOS_STRAP_ENABLE_GIT})
    add_compile_definitions(ENABLE_GIT=1)
else()
    add_compile_definitions(ENABLE_GIT=0)
endif()

add_compile_definitions(OBOS_STRAP_MLIBC=${OBOS_STRAP_MLIBC})

execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpmachine OUTPUT_VARIABLE OBOS_STRAP_HOST_TRIPLET)
string(STRIP "${OBOS_STRAP_HOST_TRIPLET}" OBOS_STRAP_HOST_TRIPLET)

add_subdirectory("src/")
