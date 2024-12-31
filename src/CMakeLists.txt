# src/CMakeLists.txt

# Copyright (c) 2024 Omar Berrow

add_executable(obos-strap
    "main.c" "clean.c" "build_pkg.c" "package.c"
    "lock.c"
)

find_package(cJSON REQUIRED)

target_link_libraries(obos-strap PRIVATE "${CJSON_LIBRARIES}")

target_include_directories(obos-strap PRIVATE "${CJSON_INCLUDE_DIR}")
