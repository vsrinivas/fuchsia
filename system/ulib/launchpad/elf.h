// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <stddef.h>

#pragma GCC visibility push(hidden)

typedef struct elf_load_info elf_load_info_t;

// Validate the ELF headers and set up for further use.
// The pointer returned must be passed to elf_load_destroy when finished.
mx_status_t elf_load_start(mx_handle_t vmo, elf_load_info_t** infop);

// Clean up and free the data structure created by elf_load_start.
void elf_load_destroy(elf_load_info_t* info);

// Check if the ELF file has a PT_INTERP header.  On success, *interp
// is NULL if it had none or a malloc'd string of the contents;
// *interp_len is strlen(*interp).
mx_status_t elf_load_get_interp(elf_load_info_t* info, mx_handle_t vmo,
                                char** interp, size_t* interp_len);

// Check if the ELF file has a PT_GNU_STACK header, and return its p_memsz.
// Returns zero if no header was found.
size_t elf_load_get_stack_size(elf_load_info_t* info);

// Load the file's segments into the process.
// If this fails, the state of the process address space is unspecified.
mx_status_t elf_load_finish(mx_handle_t vmar, elf_load_info_t* info,
                            mx_handle_t vmo,
                            mx_vaddr_t* base, mx_vaddr_t* entry);

#pragma GCC visibility pop
