// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(dje): This code is copied from crashlogger. Modest changes
// have been done. Further changes left for later.
// [C++-ification, Google code style, etc. etc. etc.]

#pragma once

#include <cstdio>
#include <memory>

#include <zircon/types.h>

#include "byte_block.h"
#include "elf_reader.h"

namespace debugserver {

typedef struct dsoinfo {
  struct dsoinfo* next;
  zx_vaddr_t base;
  zx_vaddr_t entry;
  zx_vaddr_t phdr;
  // Note: This is NULL if num_loadable_phdrs == 0.
  ElfSegmentHeader* loadable_phdrs;
  uint32_t num_loadable_phdrs;
  uint32_t phentsize, phnum;
  char buildid[ElfReader::kMaxBuildIdSize * 2 + 1];
  bool is_main_exec;
  bool debug_file_tried;
  zx_status_t debug_file_status;
  char* debug_file;
  char name[];
} dsoinfo_t;

extern dsoinfo_t* dso_fetch_list(std::shared_ptr<ByteBlock> bb, zx_vaddr_t lmap,
                                 const char* name);

extern void dso_free_list(dsoinfo_t*);

extern dsoinfo_t* dso_lookup(dsoinfo_t* dso_list, zx_vaddr_t pc);

extern dsoinfo_t* dso_get_main_exec(dsoinfo_t* dso_list);

extern void dso_print_list(FILE* out, const dsoinfo_t* dso_list);

extern void dso_vlog_list(const dsoinfo_t* dso_list);

extern zx_status_t dso_find_debug_file(dsoinfo_t* dso,
                                       const char** out_debug_file);

}  // namespace debugserver
