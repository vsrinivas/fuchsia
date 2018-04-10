// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sanitizer/scudo_interface.h>
#include <zircon/compiler.h>

// This exists to be built into every executable selected to use the
// scudo variant.  Scudo applies the options here before
// looking at the SCUDO_OPTIONS environment variable.
__EXPORT const char* __scudo_default_options(void) {
  // This macro is defined by BUILD.gn from the `scudo_default_options` GN
  // build argument.
  return SCUDO_DEFAULT_OPTIONS;
}
