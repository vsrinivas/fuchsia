// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <zircon/types.h>

#define MAX_BUILDID_SIZE 64

namespace inspector {

extern int verbosity_level;

extern void do_print_error(const char* file, int line, const char* fmt, ...);

extern void do_print_zx_error(const char* file, int line, const char* what, zx_status_t status);

#define print_error(fmt...) \
  do { \
    ::inspector::do_print_error(__FILE__, __LINE__, fmt); \
  } while (0)

#define print_zx_error(what, status) \
  do { \
    ::inspector::do_print_zx_error(__FILE__, __LINE__, \
                                   (what), static_cast<zx_status_t>(status)); \
  } while (0)

extern void do_print_debug(const char* file, int line, const char* func, const char* fmt, ...);

#define debugf(level, fmt...) \
  do { \
    if (::inspector::verbosity_level >= (level)) { \
      ::inspector::do_print_debug (__FILE__, __LINE__, __func__, fmt); \
    } \
  } while (0)

extern const char* path_basename(const char* path);

extern zx_status_t read_mem(zx_handle_t h, zx_vaddr_t vaddr, void* ptr, size_t len);

extern zx_status_t fetch_string(zx_handle_t h, zx_vaddr_t vaddr, char* ptr, size_t max);

extern zx_status_t fetch_build_id(zx_handle_t h, zx_vaddr_t base, char* buf, size_t buf_size);

}  // namespace inspector
