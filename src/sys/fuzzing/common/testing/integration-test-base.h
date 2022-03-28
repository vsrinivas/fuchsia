// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_TESTING_INTEGRATION_TEST_BASE_H_
#define SRC_SYS_FUZZING_COMMON_TESTING_INTEGRATION_TEST_BASE_H_

#include <lib/zx/channel.h>
#include <lib/zx/process.h>

#include <string>

#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/testing/async-test.h"

namespace fuzzing {

// This class is useful as a base class for test fixtures used to test integration of multiple
// components in the component fuzzing framework.
class IntegrationTestBase : public AsyncTest {
 protected:
  // Start an engine-like process from the given executable |path|, and pass it a channel to a
  // |registrar|-like service. Either object may be real or a test fake, depending on which
  // interactions are being tested.
  ZxResult<> Start(const std::string& path, zx::channel registrar);

  // Promises to wait for the previously |Start|ed process to terminate.
  ZxPromise<> AwaitTermination();

  void TearDown() override;

 private:
  zx::process process_;
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_TESTING_INTEGRATION_TEST_BASE_H_
