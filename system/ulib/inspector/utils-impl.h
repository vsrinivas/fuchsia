// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <zircon/types.h>

#include "inspector/utils.h"

#define MAX_BUILDID_SIZE 64

namespace inspector {

extern int verbosity_level;

extern void do_print_debug(const char* file, int line, const char* func, const char* fmt, ...);

#define debugf(level, fmt...) \
  do { \
    if (::inspector::verbosity_level >= (level)) { \
      do_print_debug (__FILE__, __LINE__, __func__, fmt); \
    } \
  } while (0)

extern zx_status_t read_mem(zx_handle_t h, zx_vaddr_t vaddr, void* ptr, size_t len);

extern zx_status_t fetch_string(zx_handle_t h, zx_vaddr_t vaddr, char* ptr, size_t max);

extern zx_status_t fetch_build_id(zx_handle_t h, zx_vaddr_t base, char* buf, size_t buf_size);

}  // namespace inspector
