// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <utility>

#include <fs/internal/connection.h>
#include <fs/internal/fidl_transaction.h>
#include <zxtest/zxtest.h>

TEST(FidlTransaction, Reply) {
  zx::channel client_end, server_end;
  ASSERT_OK(zx::channel::create(0, &client_end, &server_end));
  auto binding =
      std::make_shared<fs::internal::Binding>(nullptr, nullptr, std::move(server_end));
  zx_txid_t txid = 1;
  fs::internal::FidlTransaction txn(txid, binding);
  uint8_t msg_bytes[sizeof(fidl_message_header_t)] = {};
  txn.Reply(fidl::Message(fidl::BytePart::WrapFull(msg_bytes), fidl::HandlePart()));

  uint8_t received_msg_bytes[sizeof(fidl_message_header_t)] = {};
  uint32_t actual;
  ASSERT_OK(client_end.read(0, received_msg_bytes, nullptr, sizeof(received_msg_bytes), 0, &actual,
                            nullptr));
  ASSERT_EQ(actual, sizeof(fidl_message_header_t));
  ASSERT_EQ(txid, reinterpret_cast<fidl_message_header_t*>(received_msg_bytes)->txid);

  binding.reset();
}
