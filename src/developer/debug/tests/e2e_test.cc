// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"

namespace zxdb {

class E2eTest : public TestWithLoop {
 public:
  E2eTest() : session_(std::make_unique<Session>()) {}

  ~E2eTest() override = default;

  Session& session() { return *session_; }

 private:
  std::unique_ptr<Session> session_;
};

TEST_F(E2eTest, CanConnect) { SUCCEED(); }

}  // namespace zxdb
