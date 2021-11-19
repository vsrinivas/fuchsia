// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef __APPLE__

#include <stdio.h>

#include <mach-o/dyld.h>
#include <mach-o/loader.h>

#include "buildid.h"

const struct uuid_command *ExecutableUUID() {
  const struct mach_header *header = nullptr;

  for (uint32_t i = 0; i < _dyld_image_count(); i++) {
    const struct mach_header *h = _dyld_get_image_header(i);
    if (h->filetype == MH_EXECUTE) {
      header = h;
      break;
    }
  }

  if (!header) {
    return nullptr;
  }

  auto seg_ptr = reinterpret_cast<uintptr_t>(header) + sizeof(struct mach_header_64);

  for (size_t i = 0; i < header->ncmds; i++) {
    auto segment = reinterpret_cast<const struct segment_command *>(seg_ptr);
    if (segment->cmd == LC_UUID) {
      return reinterpret_cast<const struct uuid_command *>(segment);
    }
    seg_ptr += segment->cmdsize;
  }

  return nullptr;
}

int get_build_id(char out[32]) {
  auto cmd = ExecutableUUID();

  if (!cmd) {
    return -1;
  }

  int outi = 0;
  for (auto b : cmd->uuid) {
    if (outi > 30) {
      return -1;
    }

    sprintf(&out[outi], "%02x", b);
    outi += 2;
  }

  return outi;
}

#endif
