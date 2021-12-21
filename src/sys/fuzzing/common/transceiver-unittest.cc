// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/transceiver.h"

#include <gtest/gtest.h>

namespace fuzzing {
namespace {

TEST(TransceiverTest, Receive) {
  Transceiver transceiver;
  Input input({0xde, 0xad, 0xbe, 0xef, 0xfe, 0xed, 0xfa, 0xce});
  zx::socket sender;
  zx_status_t rx_result;
  Input rx_input;

  FidlInput fidl_input1;
  SyncWait sync;
  EXPECT_EQ(zx::socket::create(ZX_SOCKET_STREAM, &sender, &fidl_input1.socket), ZX_OK);
  fidl_input1.size = input.size();
  fpromise::result<Input, zx_status_t> receive_result;
  transceiver.Receive(std::move(fidl_input1), [&](zx_status_t result, Input received) {
    rx_result = result;
    rx_input = std::move(received);
    sync.Signal();
  });

  // Send data all at once.
  EXPECT_EQ(sender.write(0, input.data(), input.size(), nullptr), ZX_OK);

  sync.WaitFor("transceiver to receive contiguous data");
  EXPECT_EQ(rx_result, ZX_OK);
  EXPECT_EQ(rx_input, input);

  FidlInput fidl_input2;
  sync.Reset();
  fidl_input2.size = input.size();
  EXPECT_EQ(zx::socket::create(ZX_SOCKET_STREAM, &sender, &fidl_input2.socket), ZX_OK);
  transceiver.Receive(std::move(fidl_input2), [&](zx_status_t result, Input received) {
    rx_result = result;
    rx_input = std::move(received);
    sync.Signal();
  });

  // Send data in pieces.
  auto split = input.size() / 2;
  auto* data = input.data();
  zx::nanosleep(zx::deadline_after(zx::usec(1)));
  EXPECT_EQ(sender.write(0, &data[0], split, nullptr), ZX_OK);
  zx::nanosleep(zx::deadline_after(zx::usec(1)));
  EXPECT_EQ(sender.write(0, &data[split], input.size() - split, nullptr), ZX_OK);

  sync.WaitFor("transceiver to receive fragmented data");
  EXPECT_EQ(rx_result, ZX_OK);
  EXPECT_EQ(rx_input, input);

  // Try to receive after closing.
  FidlInput fidl_input3;
  sync.Reset();
  fidl_input3.size = input.size();
  EXPECT_EQ(zx::socket::create(ZX_SOCKET_STREAM, &sender, &fidl_input3.socket), ZX_OK);
  transceiver.Close();
  transceiver.Receive(std::move(fidl_input3), [&](zx_status_t result, Input received) {
    rx_result = result;
    sync.Signal();
  });

  sync.WaitFor("transceiver to close");
  EXPECT_EQ(rx_result, ZX_ERR_BAD_STATE);
}

TEST(TransceiverTest, Transmit) {
  Transceiver transceiver;
  Input input({0xfe, 0xed, 0xfa, 0xce});

  FidlInput fidl_input;
  EXPECT_EQ(transceiver.Transmit(input.Duplicate(), &fidl_input), ZX_OK);
  EXPECT_EQ(fidl_input.size, input.size());
  auto* data = input.data();
  Waiter waiter = [&fidl_input](zx::time deadline) {
    auto flags = ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED;
    return fidl_input.socket.wait_one(flags, deadline, nullptr);
  };
  for (size_t i = 0; i < input.size(); ++i) {
    uint8_t u8;
    EXPECT_EQ(WaitFor("socket to become readable", &waiter), ZX_OK);
    EXPECT_EQ(fidl_input.socket.read(0, &u8, sizeof(u8), nullptr), ZX_OK);
    EXPECT_EQ(data[i], u8);
  }

  // Try to transmit after closing.
  transceiver.Close();
  EXPECT_EQ(transceiver.Transmit(input.Duplicate(), &fidl_input), ZX_ERR_BAD_STATE);
}

}  // namespace
}  // namespace fuzzing
