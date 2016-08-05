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

#include "elf.h"

#include <elfload/elfload.h>
#include <elfload/elf-defines.h>

#include <magenta/syscalls.h>
#include <stdlib.h>

struct elf_load_info {
    elf_load_header_t header;
    elf_phdr_t phdrs[];
};

void elf_load_destroy(elf_load_info_t* info) {
    free(info);
}

mx_status_t elf_load_start(mx_handle_t vmo, elf_load_info_t** infop) {
    elf_load_header_t header;
    uintptr_t phoff;
    mx_status_t status = elf_load_prepare(vmo, &header, &phoff);
    if (status == NO_ERROR) {
        // Now allocate the data structure and read in the phdrs.
        size_t phdrs_size = (size_t)header.e_phnum * sizeof(elf_phdr_t);
        elf_load_info_t* info = malloc(sizeof(*info) + phdrs_size);
        if (info == NULL)
            return ERR_NO_MEMORY;
        status = elf_load_read_phdrs(vmo, info->phdrs, phoff, header.e_phnum);
        if (status == NO_ERROR) {
            info->header = header;
            *infop = info;
        } else {
            free(info);
        }
    }
    return status;
}

mx_status_t elf_load_get_interp(elf_load_info_t* info, mx_handle_t vmo,
                                char** interp, size_t* interp_len) {
    char *buffer = NULL;
    uintptr_t offset;
    if (elf_load_find_interp(info->phdrs, info->header.e_phnum,
                             &offset, interp_len)) {
        buffer = malloc(*interp_len + 1);
        if (buffer == NULL)
            return ERR_NO_MEMORY;
        mx_ssize_t n = mx_vm_object_read(vmo, buffer, offset, *interp_len);
        if (n < 0) {
            free(buffer);
            return n;
        }
        if (n != (mx_ssize_t)*interp_len) {
            free(buffer);
            return ERR_ELF_BAD_FORMAT;
        }
        buffer[*interp_len] = '\0';
    }
    *interp = buffer;
    return NO_ERROR;
}

mx_status_t elf_load_finish(mx_handle_t proc, elf_load_info_t* info,
                            mx_handle_t vmo,
                            mx_vaddr_t* base, mx_vaddr_t* entry) {
    return elf_load_map_segments(proc, &info->header, info->phdrs, vmo,
                                 base, entry);
}
