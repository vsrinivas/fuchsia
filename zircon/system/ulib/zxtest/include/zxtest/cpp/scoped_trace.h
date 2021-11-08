// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXTEST_CPP_SCOPED_TRACE_H_
#define ZXTEST_CPP_SCOPED_TRACE_H_

#include <string>

#include <zxtest/base/runner.h>
#include <zxtest/base/types.h>

namespace zxtest {
class ScopedTrace final {
 public:
  ScopedTrace() = delete;
  ScopedTrace(const zxtest::SourceLocation location, const std::string& message)
      : trace_(zxtest::Message(message, location)) {
    zxtest::Runner::GetInstance()->PushTrace(&trace_);
  }

  virtual ~ScopedTrace() final { zxtest::Runner::GetInstance()->PopTrace(); }

 private:
  zxtest::Message trace_;
};
}  // namespace zxtest

#endif  // ZXTEST_CPP_SCOPED_TRACE_H_
