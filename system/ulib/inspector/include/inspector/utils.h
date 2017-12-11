// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains random utilities used by our clients.
// Most of this is temporary pending cleaning our clients up.

#pragma once

#include <stddef.h>
#include <zircon/types.h>

#define print_error(fmt...) \
  do { \
    ::inspector::do_print_error(__FILE__, __LINE__, fmt); \
  } while (0)

#define print_zx_error(what, status) \
  do { \
    ::inspector::do_print_zx_error(__FILE__, __LINE__, \
                                      (what), static_cast<zx_status_t>(status)); \
  } while (0)

namespace inspector {

extern void set_verbosity(int level);

extern const char* path_basename(const char* path);

extern void do_print_error(const char* file, int line, const char* fmt, ...);

extern void do_print_zx_error(const char* file, int line, const char* what, zx_status_t status);

}  // namespace inspector
