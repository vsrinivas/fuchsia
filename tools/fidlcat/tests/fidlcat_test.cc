// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "zircon/third_party/ulib/musl/src/internal/libc.h"

TEST(InterceptionWorkflow, CheckSymbols) {
  // Check that the symbol is declared and defined.
  // It will be used by fidlcat to get information about the starting handles.
  ASSERT_NE(&__libc_extensions_init, nullptr);
}
