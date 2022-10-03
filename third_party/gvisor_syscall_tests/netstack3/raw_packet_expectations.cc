// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "third_party/gvisor_syscall_tests/expectations.h"

namespace netstack_syscall_test {

void AddNonPassingTests(TestMap& tests) {
  // TODO(https://fxbug.dev/110824): Stop skipping these manually once NS3 is
  // returning EPERM for raw and packet socket creation.
  SkipTest(tests, "AllInetTests/RawPacketTest.*/*");
  SkipTest(tests, "AllInetTests/CookedPacketTest.*/*");
  SkipTest(tests, "AllPacketSocketTests/*.*/*");
  SkipTest(tests, "AllRawPacketMsgSizeTest/*.*/*");
  SkipTest(tests, "BasicCookedPacketTest.WrongType");
  SkipTest(tests, "RawHDRINCL.*");
  SkipTest(tests, "RawSocketICMPTest.*");
  SkipTest(tests, "RawSocketICMPv6Test.*");
  SkipTest(tests, "AllInetTests/RawSocketTest.*/*");
  SkipTest(tests, "AllRawSocketTests/*.*/*");
  SkipTest(tests, "RawSocketTest.*");
}  // NOLINT(readability/fn_size)

}  // namespace netstack_syscall_test
