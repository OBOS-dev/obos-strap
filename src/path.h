/*
 * src/path.h
 *
 * Copyright (c) 2024 Omar Berrow
 */

#pragma once

// Final install directory.
extern const char* prefix_directory;
// Output of bootstrap commands goes here, as well as built binaries
extern const char* bootstrap_directory;
// Repositories are cloned here.
extern const char* repo_directory;
// Recipes go here.
extern const char* recipes_directory;

extern int g_argc;
extern char** g_argv;
