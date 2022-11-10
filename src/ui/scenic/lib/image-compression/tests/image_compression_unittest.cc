// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/./image-compression/image_compression.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <gtest/gtest.h>

TEST(ImageCompressionTest, Smoke) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  image_compression::App app(loop.dispatcher());
  loop.RunUntilIdle();
}
