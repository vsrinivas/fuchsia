// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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

// Load the file's segments into the process.
// If this fails, the state of the process address space is unspecified.
mx_status_t elf_load_finish(mx_handle_t proc, elf_load_info_t* info,
                            mx_handle_t vmo,
                            mx_vaddr_t* base, mx_vaddr_t* entry);

#pragma GCC visibility pop
