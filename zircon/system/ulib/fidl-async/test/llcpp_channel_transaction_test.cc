// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl-async/cpp/channel_transaction.h>

#include <zxtest/zxtest.h>

namespace {

TEST(ChannelTransactionTestCase, CloseAfterFailedReply) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToThread);

  fidl::internal::TypeErasedDispatchFn dispatch = [](void*, fidl_msg_t*, ::fidl::Transaction*) {
    return true;
  };

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  auto binding = std::make_unique<fidl::internal::SimpleBinding>(
      loop.dispatcher(), std::move(remote), nullptr, std::move(dispatch), nullptr);
  fidl::internal::ChannelTransaction txn(1, std::move(binding));

  // Sending a reply with an empty message will cause it to try to error
  // internally due to the message being too short (can't include a header).
  // This will cause the bindings to close the channel.
  txn.Reply(fidl::Message());

  // Confirm we see the channel closed
  ASSERT_OK(local.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time{}, nullptr));

  // This should be a no-op, since the channel was already closed.
  txn.Close(ZX_OK);
}

}  // namespace
