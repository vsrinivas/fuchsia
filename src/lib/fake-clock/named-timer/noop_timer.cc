// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>

#include "named_timer.h"

__WEAK __EXPORT bool create_named_deadline(char* component, size_t component_len, char* code,
                                           size_t code_len, zx_time_t duration, zx_time_t* out) {
  // Under normal execution, fake-clock is not available and deadline is not reported.
  return false;
}
