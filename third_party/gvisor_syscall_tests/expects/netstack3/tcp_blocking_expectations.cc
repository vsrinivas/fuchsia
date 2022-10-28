// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "third_party/gvisor_syscall_tests/expects/expectations.h"

namespace netstack_syscall_test {

void AddNonPassingTests(TestMap& tests) {
  // Netstack3 does not support TCP (yet).

  // Skip tests that will otherwise hang forever.
  // TODO(https://fxbug.dev/111877): un-skip some of these when the data path is ready.
  SkipTest(tests, "BlockingTCPSockets/*.*/*");
}  // NOLINT(readability/fn_size)

}  // namespace netstack_syscall_test
