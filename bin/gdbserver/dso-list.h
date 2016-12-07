// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(dje): This code is copied from crashlogger. Modest changes
// have been done. Further changes left for later.
// [C++-ification, Google code style, etc. etc. etc.]
// TODO(dje): Might be useful for this to live in the ELF library
// along with elf-util.

#pragma once

#include <magenta/types.h>

#include "elf-util.h"
#include "memory.h"

namespace debugserver {
namespace elf {

typedef struct dsoinfo {
  struct dsoinfo* next;
  mx_vaddr_t base;
  mx_vaddr_t entry;
  mx_vaddr_t phdr;
  uint32_t phentsize, phnum;
  char buildid[elf::kMaxBuildIdSize * 2 + 1];
  bool is_main_exec;
  bool debug_file_tried;
  mx_status_t debug_file_status;
  char* debug_file;
  char name[];
} dsoinfo_t;

extern dsoinfo_t* dso_fetch_list(const util::Memory& m,
                                 mx_vaddr_t lmap,
                                 const char* name);

extern void dso_free_list(dsoinfo_t*);

extern dsoinfo_t* dso_lookup(dsoinfo_t* dso_list, mx_vaddr_t pc);

extern dsoinfo_t* dso_get_main_exec(dsoinfo_t* dso_list);

extern void dso_vlog_list(dsoinfo_t* dso_list);

extern mx_status_t dso_find_debug_file(dsoinfo_t* dso,
                                       const char** out_debug_file);

}  // namespace elf
}  // namespace debugserver
