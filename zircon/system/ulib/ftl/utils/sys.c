// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

// Outputs error message when assertion fails.
void AssertError(int line, const char* file) {
  ZX_DEBUG_ASSERT_MSG(0, "AssertError at line %d, file %s\n", line, file);
}
