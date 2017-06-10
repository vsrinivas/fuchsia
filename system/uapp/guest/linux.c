// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "linux.h"

mx_status_t setup_linux(const uintptr_t addr, const int fd, uintptr_t* guest_ip) {

    // The linux kernel is just a happy ELF. We want to unpack the program segments
    // and drop them where they belong in the VMO.
    Elf64_Ehdr e_header;
    int ret = read(fd, &e_header, sizeof(e_header));
    if (ret != sizeof(e_header)) {
        fprintf(stderr, "Failed to read linux kernel elf header\n");
        return MX_ERR_IO;
    }

    // Load the program headers
    size_t p_headers_size = sizeof(Elf64_Phdr) * e_header.e_phnum;
    Elf64_Phdr* p_headers = malloc(p_headers_size);
    ret = read(fd, p_headers, p_headers_size);
    if ((size_t)ret != p_headers_size) {
        fprintf(stderr, "Failed reading linux program headers\n");
        return MX_ERR_IO;
    }

    // Load the program segments
    for (int i = 0; i < e_header.e_phnum; ++i) {
        Elf64_Phdr* p_header = &p_headers[i];

        if (p_header->p_type != PT_LOAD)
            continue;

        // Seek to the program segment
        if (lseek(fd, p_header->p_offset, SEEK_SET) < -1) {
            fprintf(stderr, "Failed seeking to linux program segment\n");
            return MX_ERR_IO;
        }
        ret = read(fd, (void*)(addr + p_header->p_paddr), p_header->p_filesz);
        if ((size_t)ret != p_header->p_filesz) {
            fprintf(stderr, "Failed reading linux program segment\n");
            return MX_ERR_IO;
        }
    }
    free(p_headers);
    *guest_ip = e_header.e_entry;
    return MX_OK;
}

