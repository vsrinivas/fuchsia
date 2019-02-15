// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sanitizer/asan_interface.h>

// ASan applies the options here before looking at the ASAN_OPTIONS
// environment variable.
const char* __asan_default_options(void) {
  // This macro is defined by BUILD.gn from the `asan_default_options` GN
  // build argument.
  return ASAN_DEFAULT_OPTIONS;
}
