// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/socket/blocking_drain.h"

#include <lib/zx/socket.h>

#include <vector>

#include "gtest/gtest.h"
#include "src/ledger/lib/socket/strings.h"

namespace ledger {
namespace {

TEST(BlockingDrain, BlockingDrainFrom) {
  zx::socket socket = WriteStringToSocket("Hello");
  std::vector<char> buffer;

  EXPECT_TRUE(BlockingDrainFrom(std::move(socket), [&](const void* data, uint32_t len) {
    const char* begin = static_cast<const char*>(data);
    buffer.insert(buffer.end(), begin, begin + len);
    return len;
  }));

  std::string message(buffer.data(), buffer.size());
  EXPECT_EQ("Hello", message);
}

// TODO(abarth): Add more tests that cover more of the codepaths. The
// BlockingDrainFrom test above only exercises the optimistic case when blocking
// isn't required.

}  // namespace
}  // namespace ledger
