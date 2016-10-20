// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <magenta/types.h>

#define MAX_BUILDID_SIZE 64

extern const char* cl_basename(const char* file);

extern int debug_level;

extern void do_print_debug(const char* file, int line, const char* func, const char* fmt, ...);

#define debugf(level, fmt...) \
  do { \
    if (debug_level >= (level)) { \
      do_print_debug (__FILE__, __LINE__, __func__, fmt); \
    } \
  } while (0)

extern void do_print_error(const char* file, int line, const char* fmt, ...);

extern void do_print_mx_error(const char* file, int line, const char* what, mx_status_t status);

#define print_error(fmt...) \
  do { \
    do_print_error(__FILE__, __LINE__, fmt); \
  } while (0)

#define print_mx_error(what, status) \
  do { \
    do_print_mx_error(__FILE__, __LINE__, \
                      (what), static_cast<mx_status_t>(status)); \
  } while (0)

extern mx_koid_t get_koid(mx_handle_t handle);

extern mx_status_t read_mem(mx_handle_t h, mx_vaddr_t vaddr, void* ptr, size_t len);

extern mx_status_t fetch_string(mx_handle_t h, mx_vaddr_t vaddr, char* ptr, size_t max);

extern mx_status_t fetch_build_id(mx_handle_t h, mx_vaddr_t base, char* buf, size_t buf_size);
