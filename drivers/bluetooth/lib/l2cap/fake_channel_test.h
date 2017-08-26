// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "garnet/drivers/bluetooth/lib/hci/hci.h"
#include "garnet/drivers/bluetooth/lib/l2cap/fake_channel.h"
#include "garnet/drivers/bluetooth/lib/l2cap/l2cap.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/macros.h"

#include "gtest/gtest.h"

namespace bluetooth {
namespace l2cap {
namespace testing {

// Provides a common GTest harness base class for protocols tests that operate
// over a L2CAP channel. This harness provides:
//
//   * A simple way to initialize and access a FakeChannel.
//   * A message loop.
//   * Basic command<->response expectation.
class FakeChannelTest : public ::testing::Test {
 public:
  FakeChannelTest() = default;
  ~FakeChannelTest() override = default;

 protected:
  struct ChannelOptions {
    explicit ChannelOptions(ChannelId id) : id(id) {}

    ChannelId id;
    hci::ConnectionHandle conn_handle = 0x0001;
  };

  // Creates a new FakeChannel and returns it. A fxl::WeakPtr to the returned
  // channel is stored internally so that the returned channel can be accessed
  // by tests even if its ownership is passed outside of the test harness.
  std::unique_ptr<Channel> CreateFakeChannel(const ChannelOptions& options);

  // Runs the message loop for at most |timeout_seconds| seconds.
  void RunMessageLoop(int64_t timeout_seconds = 5);

  // Emulates the receipt of |packet| and returns true if a response that
  // matches |expected_response| is sent back over the underlying FakeChannel.
  // Returns false if no such response is received or no FakeChannel has been
  // initialized via CreateFakeChannel().
  //
  // NOTE: This overwrites the underlying FakeChannel's "send callback" by
  // calling FakeChannel::SetSendCallback().
  bool ReceiveAndExpect(const common::ByteBuffer& packet,
                        const common::ByteBuffer& expected_response);

  fxl::WeakPtr<FakeChannel> fake_chan() const { return fake_chan_; }

  // Returns a pointer to the internal message loop.
  fsl::MessageLoop* message_loop() { return &message_loop_; }

 private:
  fxl::WeakPtr<FakeChannel> fake_chan_;
  fsl::MessageLoop message_loop_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeChannelTest);
};

}  // namespace testing
}  // namespace l2cap
}  // namespace bluetooth
