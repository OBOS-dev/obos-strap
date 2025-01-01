# CMakeLists.txt
#
# Copyright (c) 2024 Omar Berrow

cmake_minimum_required(VERSION 3.10)

set (CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/out/")

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
find_package(PkgConfig)
if (PkgConfig_FOUND)
    pkg_check_modules(LIBGIT2 libgit2)
else()
    set (LIBGIT2_FOUND false)
endif ()

if (${CURL_FOUND})
    add_compile_definitions(HAS_LIBCURL=1)
    set(HAS_CURL 1)
else()
    set(HAS_CURL 0)
endif()

if (${LIBGIT2_FOUND})
    add_compile_definitions(HAS_LIBCURL=1)
endif()

add_subdirectory("src/")
