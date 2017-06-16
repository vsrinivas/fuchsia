// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a minimal interface for the logic of loading ELF files.  It
// is specifically designed to work entirely without memory allocation
// or long-lived data variables.  Callers are responsible for all memory
// allocation.  This code itself is position-independent code that does
// not need any writable memory anywhere but the stack.

#pragma once

#include <elf.h>
#include <magenta/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#pragma GCC visibility push(hidden)

#ifdef _LP64
# define MY_ELFCLASS ELFCLASS64
typedef Elf64_Ehdr elf_ehdr_t;
typedef Elf64_Phdr elf_phdr_t;
#else
# define MY_ELFCLASS ELFCLASS32
typedef Elf32_Ehdr elf_ehdr_t;
typedef Elf32_Phdr elf_phdr_t;
#endif

typedef struct {
    mx_vaddr_t e_entry;
    uint_fast16_t e_phnum;
} elf_load_header_t;

// These routines use this error code to indicate an invalid file format,
// including wrong machine, wrong endian, etc. as well as a truncated file.
#define ERR_ELF_BAD_FORMAT MX_ERR_NOT_FOUND

__BEGIN_CDECLS

// Validate the ELF headers and fill in basic header information. 'hdr_buf'
// represents bytes already read from the start of the file.
mx_status_t elf_load_prepare(mx_handle_t vmo, const void* hdr_buf, size_t buf_sz,
                             elf_load_header_t* header, uintptr_t* phoff);

// Read the ELF program headers in.
mx_status_t elf_load_read_phdrs(mx_handle_t vmo, elf_phdr_t* phdrs,
                                uintptr_t phoff, size_t phnum);

// Load the image into the process.
mx_status_t elf_load_map_segments(mx_handle_t vmar,
                                  const elf_load_header_t* header,
                                  const elf_phdr_t* phdrs,
                                  mx_handle_t vmo,
                                  mx_handle_t* segments_vmar,
                                  mx_vaddr_t* bias, mx_vaddr_t* entry);

// Locate the PT_INTERP program header and extract its bounds in the file.
// Returns false if there was no PT_INTERP.
bool elf_load_find_interp(const elf_phdr_t* phdrs, size_t phnum,
                          uintptr_t* interp_off, size_t* interp_len);

__END_CDECLS

#pragma GCC visibility pop
