/*
 * src/path.h
 *
 * Copyright (c) 2024 Omar Berrow
 */

#pragma once

#include <stdbool.h>

// Final install directory.
extern const char* prefix_directory;
// Final install directory for host packages.
extern const char* host_prefix_directory;
// Binary packages go here.
extern const char* binary_package_directory;
// Output of bootstrap commands goes here, as well as built binaries
extern const char* bootstrap_directory;
// Repositories are cloned here.
extern const char* repo_directory;
// Recipes go here.
extern const char* recipes_directory;
// Cached info about built packages goes here.
extern const char* pkg_info_directory;
// The repository root directory (i.e., the CWD at start)
extern const char* root_directory;

extern int g_argc;
extern char** g_argv;

extern struct config {
	const char* target_triplet;
	bool cross_compiling;
	bool binary_packages_default;
	const char* host_triplet;
} g_config;
