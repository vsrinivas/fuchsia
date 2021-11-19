// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef __linux__

#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <stddef.h>
#include <stdio.h>

#include <algorithm>

#include "buildid.h"

static int iter(struct dl_phdr_info *info, size_t sz, void *vctx) {
  char *out = reinterpret_cast<char *>(vctx);

  // Observed on linux: dlpi_name for the main process is empty
  if (strlen(info->dlpi_name) != 0) {
    return 0;
  }

  for (size_t i = 0; i < info->dlpi_phnum; i++) {
    if (info->dlpi_phdr[i].p_type != PT_NOTE) {
      continue;
    }

    uintptr_t note_ptr = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr;

    uintptr_t end = note_ptr + info->dlpi_phdr[i].p_memsz;

    while (note_ptr + sizeof(ElfW(Nhdr)) < end) {
      auto note = reinterpret_cast<ElfW(Nhdr) *>(note_ptr);
      auto name = reinterpret_cast<char *>(note_ptr + sizeof(ElfW(Nhdr)));

      if (note->n_type == NT_GNU_BUILD_ID && note->n_namesz == 4 && memcmp(name, "GNU", 4) == 0) {
        uint8_t *build_id =
            reinterpret_cast<uint8_t *>(note_ptr + sizeof(ElfW(Nhdr)) + note->n_namesz);

        for (size_t i = 0; i < std::min(16u, note->n_descsz); i++) {
          sprintf(&out[i * 2], "%02x", build_id[i]);
        }

        return std::min(32u, note->n_descsz * 2);
      }

      note_ptr = note_ptr + sizeof(ElfW(Nhdr)) + note->n_namesz + note->n_descsz;
    }
  }

  return 0;
}

int get_build_id(char out[32]) { return dl_iterate_phdr(iter, out); }

#endif
