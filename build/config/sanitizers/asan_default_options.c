// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sanitizer/asan_interface.h>

// TODO(45047): BUILD.gn machinery passes the suppress-lsan.ld input linker
// script to define this symbol as some nonzero address (never to be
// dereferenced, just observed as nonzero).
__attribute__((weak)) extern struct {
} _FUCHSIA_SUPPRESS_LSAN;

// ASan applies the options here before looking at the ASAN_OPTIONS
// environment variable.
const char* __asan_default_options(void) {
  // TODO(45047): Remove this later.  If the magic cookie was linked in,
  // add LSan suppression to the compiled-in options list.
  if (&_FUCHSIA_SUPPRESS_LSAN) {
    return ASAN_DEFAULT_OPTIONS ":detect_leaks=0";
  }

  // This macro is defined by BUILD.gn from the `asan_default_options` GN
  // build argument.
  return ASAN_DEFAULT_OPTIONS;
}
