// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_LAUNCH_COUNTER_H_
#define SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_LAUNCH_COUNTER_H_

#include <lib/modular/testing/cpp/fake_component.h>

#include "src/lib/fxl/memory/weak_ptr.h"

namespace modular_testing {

class LaunchCounter {
 public:
  LaunchCounter();

  // Constructs an InterceptOptions struct that tracks the number of times the `launch_handler`
  // in the given InterceptOptions has been called.
  modular_testing::TestHarnessBuilder::InterceptOptions WrapInterceptOptions(
      modular_testing::TestHarnessBuilder::InterceptOptions options);

  int launch_count() const { return launch_count_; }

 private:
  int launch_count_ = 0;

  fxl::WeakPtrFactory<LaunchCounter> weak_factory_;
};

}  // namespace modular_testing

#endif  // SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_LAUNCH_COUNTER_H_
