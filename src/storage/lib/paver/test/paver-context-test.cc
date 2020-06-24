// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/paver-context.h"

#include <zxtest/zxtest.h>

#include "src/storage/lib/paver/astro.h"

namespace {
TEST(PaverContextTest, Initialize) {
  paver::Context context;
  ASSERT_OK(context.Initialize<paver::AstroPartitionerContext>([]() { return zx::ok(nullptr); }));
}

TEST(PaverContextTest, Call) {
  paver::Context context;
  ASSERT_OK(context.Initialize<paver::AstroPartitionerContext>(
      []() { return zx::ok(std::make_unique<paver::AstroPartitionerContext>(nullptr)); }));
  ASSERT_OK(context.Call<paver::AstroPartitionerContext>([](auto* ctx) { return zx::ok(); }));
}

TEST(PaverContextTest, CallWithUninitializedContext) {
  paver::Context context;
  ASSERT_NOT_OK(context.Call<paver::AstroPartitionerContext>([](auto* ctx) { return zx::ok(); }));
}
}  // namespace
