// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <elf/elf.h>
#include <elfload/elfload.h>

struct elf_load_info {
  elf_load_header_t header;
  elf_phdr_t phdrs[];
};

typedef struct elf_load_info elf_load_info_t;

static void elf_load_destroy(elf_load_info_t* info) { free(info); }

static zx_status_t elf_load_start(zx_handle_t vmo, const void* hdr_buf, size_t buf_sz,
                           elf_load_info_t** infop) {
  elf_load_header_t header;
  uintptr_t phoff;
  zx_status_t status = elf_load_prepare(vmo, hdr_buf, buf_sz, &header, &phoff);
  if (status == ZX_OK) {
    // Now allocate the data structure and read in the phdrs.
    size_t phdrs_size = (size_t)header.e_phnum * sizeof(elf_phdr_t);
    elf_load_info_t* info = malloc(sizeof(*info) + phdrs_size);
    if (info == NULL)
      return ZX_ERR_NO_MEMORY;
    status = elf_load_read_phdrs(vmo, info->phdrs, phoff, header.e_phnum);
    if (status == ZX_OK) {
      info->header = header;
      *infop = info;
    } else {
      free(info);
    }
  }
  return status;
}

static zx_status_t elf_load_finish(zx_handle_t vmar, elf_load_info_t* info, zx_handle_t vmo,
                            zx_handle_t* segments_vmar, zx_vaddr_t* base, zx_vaddr_t* entry) {
  return elf_load_map_segments(vmar, &info->header, info->phdrs, vmo, segments_vmar, base, entry);
}

zx_status_t elf_load_extra(zx_handle_t vmar, zx_handle_t vmo, zx_vaddr_t* base, zx_vaddr_t* entry) {
  if (vmo == ZX_HANDLE_INVALID)
    return ZX_ERR_INVALID_ARGS;
  elf_load_info_t* elf;
  zx_status_t status = elf_load_start(vmo, NULL, 0, &elf);
  if (status != ZX_OK) {
    return status;
  }
  status = elf_load_finish(vmar, elf, vmo, NULL, base, entry);
  if (status != ZX_OK) {
    return status;
  }
  elf_load_destroy(elf);
  return ZX_OK;
}
