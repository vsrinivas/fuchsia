// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "paver-context.h"

#include <zxtest/zxtest.h>

namespace {
TEST(PaverContextTest, Initialize) {
  paver::Context context;
  ASSERT_OK(context.Initialize<paver::AstroPartitionerContext>([](auto* out) { return ZX_OK; }));
}

TEST(PaverContextTest, Call) {
  paver::Context context;
  ASSERT_OK(context.Initialize<paver::AstroPartitionerContext>([](auto* out) {
    *out = std::make_unique<paver::AstroPartitionerContext>(nullptr);
    return ZX_OK;
  }));
  ASSERT_OK(context.Call<paver::AstroPartitionerContext>([](auto* ctx) { return ZX_OK; }));
}

TEST(PaverContextTest, CallWithUninitializedContext) {
  paver::Context context;
  ASSERT_NOT_OK(context.Call<paver::AstroPartitionerContext>([](auto* ctx) { return ZX_OK; }));
}
}  // namespace
