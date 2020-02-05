// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fs/test_support/fixtures.h"

namespace fs {
namespace {

// This is just a trivial smoke test to make sure that volumes are created and
// mounted. "Real" code is exercised on the tests that actually use the fixtures
// as the base for specific fixtures.

// Go over the environment and test fixture logic.
// Note that this requires a filesystem, so Blobfs is used.
TEST_F(FilesystemTest, Trivial) {}

TEST_F(FilesystemTestWithFvm, Trivial) {}

class PowerTest : public FilesystemTestWithFvm {
 public:
  PowerTest() : runner_(this) {}
  void RunWithFailures(std::function<void()> function) { runner_.Run(function); }
  void RunWithRestart(std::function<void()> function) { runner_.RunWithRestart(function); }

 protected:
  PowerFailureRunner runner_;
};

void DoSomeFsOperations() {}

TEST_F(PowerTest, Trivial) { RunWithFailures(&DoSomeFsOperations); }

TEST_F(PowerTest, TrivialWithRestart) { RunWithRestart(&DoSomeFsOperations); }

}  // namespace
}  // namespace fs
