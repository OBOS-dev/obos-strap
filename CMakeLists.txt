# CMakeLists.txt
#
# Copyright (c) 2024 Omar Berrow

cmake_minimum_required(VERSION 3.10)

project (
    obos-strap
    LANGUAGES C
    DESCRIPTION "Yet another build tool for operating systems"
    HOMEPAGE_URL https://github.com/oberrow/obos-strap
)

add_subdirectory("src/")
