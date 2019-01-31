// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>
#include <stddef.h>

#pragma GCC visibility push(hidden)

typedef struct elf_load_info elf_load_info_t;

// Validate the ELF headers and set up for further use.
// The pointer returned must be passed to elf_load_destroy when finished.
zx_status_t elf_load_start(zx_handle_t vmo, const void* buf, size_t buf_sz,
                           elf_load_info_t** infop);

// Clean up and free the data structure created by elf_load_start.
void elf_load_destroy(elf_load_info_t* info);

// Check if the ELF file has a PT_INTERP header.  On success, *interp
// is NULL if it had none or a malloc'd string of the contents;
// *interp_len is strlen(*interp).
zx_status_t elf_load_get_interp(elf_load_info_t* info, zx_handle_t vmo,
                                char** interp, size_t* interp_len);

// Check if the ELF file has a PT_GNU_STACK header, and return its p_memsz.
// Returns zero if no header was found.
size_t elf_load_get_stack_size(elf_load_info_t* info);

// Load the file's segments into the process.
// If this fails, the state of the process address space is unspecified.
// Regardless of success/failure this does not consume |vmo|.
zx_status_t elf_load_finish(zx_handle_t vmar, elf_load_info_t* info,
                            zx_handle_t vmo,
                            zx_handle_t* segments_vmar,
                            zx_vaddr_t* base, zx_vaddr_t* entry);

#pragma GCC visibility pop
