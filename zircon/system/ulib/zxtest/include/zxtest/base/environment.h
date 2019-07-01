// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXTEST_BASE_ENVIRONMENT_H_
#define ZXTEST_BASE_ENVIRONMENT_H_

namespace zxtest {
// Interface for global state set up. This is executed once per iteration, before any test case set
// up, and after all test cases are torn down.
class Environment {
 public:
  virtual ~Environment() = default;

  virtual void SetUp() = 0;
  virtual void TearDown() = 0;
};

}  // namespace zxtest

#endif  // ZXTEST_BASE_ENVIRONMENT_H_
