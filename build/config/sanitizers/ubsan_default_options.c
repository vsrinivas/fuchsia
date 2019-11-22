// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sanitizer/ubsan_interface.h>

// UBSan applies the options here before looking at the UBSAN_OPTIONS
// environment variable.
const char* __ubsan_default_options(void) {
  // This macro is defined by BUILD.gn from the `ubsan_default_options` GN
  // build argument.
  return UBSAN_DEFAULT_OPTIONS;
}
