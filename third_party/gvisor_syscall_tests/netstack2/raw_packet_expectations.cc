// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "third_party/gvisor_syscall_tests/expectations.h"

namespace netstack_syscall_test {

void AddNonPassingTests(TestMap& tests) {
  // https://fxbug.dev/82596
  ExpectFailure(tests, "AllInetTests/RawSocketTest.SetSocketDetachFilterNoInstalledFilter/*");
  ExpectFailure(tests, "AllInetTests/RawPacketTest.SetSocketDetachFilterNoInstalledFilter/*");

  // https://fxbug.dev/46102
  ExpectFailure(tests, "RawSocketTest.ReceiveIPPacketInfo");

  // TODO(https://fxbug.dev/87235): Expect success once Fuchsia supports sending
  // packets with the maximum possible payload length. Currently, this is
  // limited by a channel's maximum message size.
  ExpectFailure(tests, "AllRawPacketMsgSizeTest/RawPacketMsgSizeTest.SendTooLong/*");

  // https://fxbug.dev/90501
  ExpectFailure(tests, "RawSocketICMPTest.IPv6ChecksumNotSupported");
  ExpectFailure(tests, "RawSocketICMPTest.ICMPv6FilterNotSupported");
}  // NOLINT(readability/fn_size)

}  // namespace netstack_syscall_test
