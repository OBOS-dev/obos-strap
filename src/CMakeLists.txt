# src/CMakeLists.txt

# Copyright (c) 2024 Omar Berrow

add_executable(obos-strap
    "main.c" "clean.c" "build_pkg.c" "package.c"
    "lock.c" "cmd.c"
)

target_link_libraries(obos-strap PRIVATE "${CJSON_LIBRARIES}")
target_include_directories(obos-strap PRIVATE "${CJSON_INCLUDE_DIR}")

if (HAS_CURL)
    target_include_directories(obos-strap PRIVATE ${CURL_INCLUDE_DIR})
    target_link_libraries(obos-strap PRIVATE ${CURL_LIBRARIES})
endif()

if (HAS_LIBGIT2)
    target_include_directories(obos-strap PRIVATE "${LIBGIT2_INCLUDE_DIRS}")
    target_link_libraries(obos-strap PRIVATE "${LIBGIT2_LINK_LIBRARIES}")
endif()
