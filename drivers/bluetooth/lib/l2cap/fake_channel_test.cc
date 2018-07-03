// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_channel_test.h"

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"

namespace btlib {
namespace l2cap {
namespace testing {

fbl::RefPtr<FakeChannel> FakeChannelTest::CreateFakeChannel(
    const ChannelOptions& options) {
  auto fake_chan = fbl::AdoptRef(new FakeChannel(
      options.id, options.remote_id, options.conn_handle, options.link_type));
  fake_chan_ = fake_chan->AsWeakPtr();
  return fake_chan;
}

bool FakeChannelTest::Expect(const common::ByteBuffer& expected) {
  if (!fake_chan())
    return false;

  bool success = false;
  auto cb = [&expected, &success, this](auto cb_packet) {
    success = common::ContainersEqual(expected, *cb_packet);
  };

  fake_chan()->SetSendCallback(cb, dispatcher());
  RunLoopUntilIdle();

  return success;
}

bool FakeChannelTest::ReceiveAndExpect(
    const common::ByteBuffer& packet,
    const common::ByteBuffer& expected_response) {
  if (!fake_chan())
    return false;

  fake_chan()->Receive(packet);

  return Expect(expected_response);
}

}  // namespace testing
}  // namespace l2cap
}  // namespace btlib
