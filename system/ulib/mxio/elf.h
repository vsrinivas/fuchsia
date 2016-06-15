// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015 Travis Geiselbrecht
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

#include "elf_defines.h"

#include <magenta/types.h>
#include <runtime/compiler.h>
#include <stdbool.h>
#include <sys/types.h>

/* based on our bitness, support 32 or 64 bit elf */
#if IS_64BIT
#define WITH_ELF64 1
#else
#define WITH_ELF32 1
#endif

__BEGIN_CDECLS

/* based on our bitness, support 32 or 64 bit elf */
#if IS_64BIT
#define WITH_ELF64 1
#else
#define WITH_ELF32 1
#endif

/* api */
typedef struct elf_handle elf_handle_t;

// read data from elf image into buffer
typedef ssize_t (*elf_read_hook_t)(elf_handle_t*, void* buf, uintptr_t offset, size_t len);

// read data (or zeros if offset==0) from elf image into new process
typedef mx_status_t (*elf_load_hook_t)(elf_handle_t*, uintptr_t vaddr, uintptr_t offset, size_t len);

struct elf_handle {
    bool open;

    // read hook to load binary out of memory
    elf_read_hook_t read_hook;
    // memory allocation callback
    elf_load_hook_t load_hook;

    // handle to process to load into
    mx_handle_t proc;

    // data for callbacks
    void* arg;

// loaded info about the elf file
#if WITH_ELF32
    struct Elf32_Ehdr eheader;   // a copy of the main elf header
    struct Elf32_Phdr* pheaders; // a pointer to a buffer of program headers
#else
    struct Elf64_Ehdr eheader; // a copy of the main elf header
    struct Elf64_Phdr* pheaders; // a pointer to a buffer of program headers
#endif

    // current vmo
    mx_handle_t vmo;
    uintptr_t vmo_addr;

    uintptr_t load_address;
    uintptr_t entry;
};

mx_status_t elf_open_handle(elf_handle_t* handle, mx_handle_t proc_handle,
                            elf_read_hook_t rh, elf_load_hook_t lh, void* arg);
mx_status_t elf_load(elf_handle_t* handle);
void elf_close_handle(elf_handle_t* handle);

__END_CDECLS
