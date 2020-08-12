// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/types.h>

#include <fidl/c_generator.h>
#include <zxtest/zxtest.h>

namespace {

// TODO(fxbug.dev/56727): Eventually, fidl will have a first-class definition of
// the channel transport, and static_asserting the equality of these
// two constants will not be fidl's job.
TEST(CGeneratorTests, ChannelMaxHandles) {
  static_assert(fidl::CGenerator::kChannelMaxMessageHandles == ZX_CHANNEL_MAX_MSG_HANDLES);
}

}  // namespace
