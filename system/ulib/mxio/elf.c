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

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elf.h"

#include <magenta/syscalls.h>
#include <magenta/types.h>

//#define LTRACEF(fmt...) printf(fmt)
#define LTRACEF(fmt...) \
    do {                \
    } while (0)

/* conditionally define a 32 or 64 bit version of the data structures
 * we care about, based on our bitness.
 */
#if WITH_ELF32
typedef struct Elf32_Ehdr elf_ehdr_t;
typedef struct Elf32_Phdr elf_phdr_t;

#define ELF_OFF_PRINT_U "%u"
#define ELF_OFF_PRINT_X "%x"
#define ELF_ADDR_PRINT_U "%u"
#define ELF_ADDR_PRINT_X "%x"
#else
typedef struct Elf64_Ehdr elf_ehdr_t;
typedef struct Elf64_Phdr elf_phdr_t;

#define ELF_OFF_PRINT_U "%llu"
#define ELF_OFF_PRINT_X "%llx"
#define ELF_ADDR_PRINT_U "%llu"
#define ELF_ADDR_PRINT_X "%llx"
#endif

mx_status_t elf_open_handle(elf_handle_t* handle, mx_handle_t proc_handle,
                            elf_read_hook_t rh, elf_load_hook_t lh, void* arg) {
    if (!handle)
        return ERR_INVALID_ARGS;
    if (!proc_handle)
        return ERR_INVALID_ARGS;
    if (!rh)
        return ERR_INVALID_ARGS;
    if (!lh)
        return ERR_INVALID_ARGS;

    memset(handle, 0, sizeof(*handle));

    handle->proc = proc_handle;
    handle->read_hook = rh;
    handle->load_hook = lh;
    handle->arg = arg;

    handle->open = true;

    return NO_ERROR;
}

void elf_close_handle(elf_handle_t* handle) {
    if (!handle || !handle->open)
        return;

    handle->open = false;

    free(handle->pheaders);

    if (handle->vmo > 0)
        _magenta_handle_close(handle->vmo);
}

static int verify_eheader(const void* header) {
    const elf_ehdr_t* eheader = header;

    if (memcmp(eheader->e_ident, ELF_MAGIC, 4) != 0)
        return ERR_NOT_FOUND;

#if WITH_ELF32
    if (eheader->e_ident[EI_CLASS] != ELFCLASS32)
        return ERR_NOT_FOUND;
#else
    if (eheader->e_ident[EI_CLASS] != ELFCLASS64)
        return ERR_NOT_FOUND;
#endif

#if BYTE_ORDER == LITTLE_ENDIAN
    if (eheader->e_ident[EI_DATA] != ELFDATA2LSB)
        return ERR_NOT_FOUND;
#elif BYTE_ORDER == BIG_ENDIAN
    if (eheader->e_ident[EI_DATA] != ELFDATA2MSB)
        return ERR_NOT_FOUND;
#endif

    if (eheader->e_ident[EI_VERSION] != EV_CURRENT)
        return ERR_NOT_FOUND;

    if (eheader->e_phoff == 0)
        return ERR_NOT_FOUND;

    if (eheader->e_phentsize < sizeof(elf_phdr_t))
        return ERR_NOT_FOUND;

#if ARCH_ARM
    if (eheader->e_machine != EM_ARM)
        return ERR_NOT_FOUND;
#elif ARCH_ARM64
    if (eheader->e_machine != EM_AARCH64)
        return ERR_NOT_FOUND;
#elif ARCH_X86_64
    if (eheader->e_machine != EM_X86_64)
        return ERR_NOT_FOUND;
#elif ARCH_X86_32
    if (eheader->e_machine != EM_386)
        return ERR_NOT_FOUND;
#elif ARCH_MICROBLAZE
    if (eheader->e_machine != EM_MICROBLAZE)
        return ERR_NOT_FOUND;
#else
#error find proper EM_ define for your machine
#endif

    return NO_ERROR;
}

mx_status_t elf_load(elf_handle_t* handle) {
    if (!handle)
        return ERR_INVALID_ARGS;
    if (!handle->open)
        return ERR_NOT_READY;

    // validate that this is an ELF file
    ssize_t readerr = handle->read_hook(handle, &handle->eheader,
                                        0, sizeof(handle->eheader));
    if (readerr < (ssize_t)sizeof(handle->eheader)) {
        LTRACEF("couldn't read elf header\n");
        return ERR_NOT_FOUND;
    }

    if (verify_eheader(&handle->eheader)) {
        LTRACEF("header not valid\n");
        return ERR_NOT_FOUND;
    }

    // sanity check number of program headers
    LTRACEF("number of program headers %u, entry size %u\n",
            handle->eheader.e_phnum, handle->eheader.e_phentsize);
    if (handle->eheader.e_phnum > 16 ||
        handle->eheader.e_phentsize != sizeof(elf_phdr_t)) {
        LTRACEF("too many program headers or bad size\n");
        return ERR_NO_MEMORY;
    }

    // allocate and read in the program headers
    handle->pheaders = calloc(1, handle->eheader.e_phnum * handle->eheader.e_phentsize);
    if (!handle->pheaders) {
        LTRACEF("failed to allocate memory for program headers\n");
        return ERR_NO_MEMORY;
    }

    readerr = handle->read_hook(handle, handle->pheaders, handle->eheader.e_phoff,
                                handle->eheader.e_phnum * handle->eheader.e_phentsize);
    if (readerr < (ssize_t)(handle->eheader.e_phnum * handle->eheader.e_phentsize)) {
        LTRACEF("failed to read program headers\n");
        return ERR_NO_MEMORY;
    }

    LTRACEF("program headers:\n");
    for (uint i = 0; i < handle->eheader.e_phnum; i++) {
        // parse the program headers
        elf_phdr_t* pheader = &handle->pheaders[i];

        LTRACEF("%u: type %u offset 0x" ELF_OFF_PRINT_X " vaddr " ELF_ADDR_PRINT_X " paddr " ELF_ADDR_PRINT_X " memsiz " ELF_ADDR_PRINT_U " filesize " ELF_ADDR_PRINT_U
                " flags 0x%x\n",
                i, pheader->p_type, pheader->p_offset, pheader->p_vaddr,
                pheader->p_paddr, pheader->p_memsz, pheader->p_filesz, pheader->p_flags);

        // we only care about PT_LOAD segments at the moment
        if (pheader->p_type == PT_LOAD) {
            // allocate a block of memory to back the segment
            if (handle->vmo != 0) {
                _magenta_handle_close(handle->vmo);
            }

            // Some binaries declare program headers that
            // do not start aligned to a page boundary.
            // Fix that up so we don't make the vmo mapping
            // unhappy later, and things get loaded correctly.
            uint64_t align = 0;
            handle->vmo_addr = (uintptr_t)pheader->p_vaddr;
            if (handle->vmo_addr & (PAGE_SIZE - 1)) {
                handle->vmo_addr &= (~(PAGE_SIZE - 1));
                align = PAGE_SIZE - (handle->vmo_addr & (PAGE_SIZE - 1));
            }

            handle->vmo = _magenta_vm_object_create(pheader->p_memsz + align);
            if (handle->vmo < 0) {
                LTRACEF("failed to allocate VMO to back elf segment at 0x%lx\n", handle->vmo_addr);
                return ERR_NO_MEMORY;
            }

            // map it in the target address space
            uint32_t mx_flags = MX_VM_FLAG_FIXED;
            mx_flags |= (pheader->p_flags & PF_R) ? MX_VM_FLAG_PERM_READ : 0;
            mx_flags |= (pheader->p_flags & PF_W) ? MX_VM_FLAG_PERM_WRITE : 0;
            mx_flags |= (pheader->p_flags & PF_X) ? MX_VM_FLAG_PERM_EXECUTE : 0;
            uintptr_t ptr = handle->vmo_addr;
            mx_status_t status = _magenta_process_vm_map(handle->proc, handle->vmo, 0,
                                                         pheader->p_memsz + align, &ptr, mx_flags);
            if (status < 0) {
                LTRACEF("failed to map VMO to back elf segment at 0x%lx\n", handle->vmo_addr);
                return ERR_NO_MEMORY;
            }

            // read the file portion of the segment into memory at vaddr
            readerr = handle->load_hook(handle, pheader->p_vaddr,
                                        pheader->p_offset, pheader->p_filesz);
            if (readerr < (ssize_t)pheader->p_filesz) {
                LTRACEF("error %ld reading program header %u\n", readerr, i);
                return (readerr < 0) ? readerr : ERR_IO;
            }
        }
    }

    // save the entry point
    handle->entry = handle->eheader.e_entry;

    return NO_ERROR;
}
