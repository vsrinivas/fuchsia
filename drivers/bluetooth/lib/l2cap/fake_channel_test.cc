// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_channel_test.h"

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"

namespace bluetooth {
namespace l2cap {
namespace testing {

std::unique_ptr<Channel> FakeChannelTest::CreateFakeChannel(
    const ChannelOptions& options) {
  auto fake_chan =
      std::make_unique<FakeChannel>(options.id, options.conn_handle);
  fake_chan_ = fake_chan->AsWeakPtr();
  return fake_chan;
}

bool FakeChannelTest::ReceiveAndExpect(
    const common::ByteBuffer& packet,
    const common::ByteBuffer& expected_response) {
  if (!fake_chan_)
    return false;

  bool success = false;
  auto cb = [&expected_response, &success, this](auto cb_packet) {
    success = common::ContainersEqual(expected_response, *cb_packet);
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  fake_chan()->SetSendCallback(cb, message_loop()->task_runner());
  fake_chan()->Receive(packet);

  RunMessageLoop();

  return success;
}

}  // namespace testing
}  // namespace l2cap
}  // namespace bluetooth
