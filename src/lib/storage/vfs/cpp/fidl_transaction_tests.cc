// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/llcpp/message.h>

#include <utility>

#include <zxtest/zxtest.h>

#include "src/lib/storage/vfs/cpp/connection.h"
#include "src/lib/storage/vfs/cpp/fidl_transaction.h"
#include "src/lib/storage/vfs/cpp/node_connection.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"

TEST(FidlTransaction, Reply) {
  zx::channel client_end, server_end;
  ASSERT_OK(zx::channel::create(0, &client_end, &server_end));

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fs::SynchronousVfs vfs(loop.dispatcher());

  auto dir = fbl::MakeRefCounted<fs::PseudoDir>();
  fs::internal::NodeConnection connection(&vfs, dir, fs::VnodeProtocol::kDirectory, {});
  auto binding = std::make_shared<fs::internal::Binding>(&connection, loop.dispatcher(),
                                                         std::move(server_end));
  zx_txid_t txid = 1;
  fs::internal::FidlTransaction txn(txid, binding);
  fidl_message_header_t message_header;
  zx_channel_iovec_t iovec = {
      .buffer = &message_header,
      .capacity = sizeof(message_header),
      .reserved = 0,
  };
  fidl_outgoing_msg_t c_msg = {
      .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
      .iovec =
          {
              .iovecs = &iovec,
              .num_iovecs = 1,
          },
  };
  auto message = fidl::OutgoingMessage::FromEncodedCMessage(&c_msg);
  ASSERT_OK(txn.Reply(&message));

  uint8_t received_msg_bytes[sizeof(fidl_message_header_t)] = {};
  uint32_t actual;
  ASSERT_OK(client_end.read(0, received_msg_bytes, nullptr, sizeof(received_msg_bytes), 0, &actual,
                            nullptr));
  ASSERT_EQ(actual, sizeof(fidl_message_header_t));
  ASSERT_EQ(txid, reinterpret_cast<fidl_message_header_t*>(received_msg_bytes)->txid);
}
