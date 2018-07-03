// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/testing/mock_base.h"

#include <lib/fxl/logging.h>

#include "gtest/gtest.h"

namespace modular {
namespace testing {

MockBase::MockBase() = default;
MockBase::~MockBase() = default;

void MockBase::ExpectCalledOnce(const std::string& func) {
  EXPECT_EQ(1U, counts.count(func)) << "Expected 1 invocation of '" << func
                                    << "' but got " << counts.count(func);
  if (counts.count(func) > 0) {
    EXPECT_EQ(1U, counts[func]);
    counts.erase(func);
  }
}

void MockBase::ClearCalls() { counts.clear(); }

void MockBase::ExpectNoOtherCalls() {
  EXPECT_TRUE(counts.empty());
  for (const auto& c : counts) {
    FXL_LOG(INFO) << "    Unexpected call: " << c.first;
  }
}

}  // namespace testing
}  // namespace modular
