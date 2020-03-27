// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/txn_header.h>
#include <lib/fidl-async/cpp/client_base.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <zxtest/zxtest.h>

#include <mutex>
#include <thread>
#include <unordered_set>

namespace fidl {
namespace internal {
namespace {

class TestClient : public ClientBase {
 public:
  TestClient(zx::channel channel, async_dispatcher_t* dispatcher,
             TypeErasedOnUnboundFn on_unbound)
      : ClientBase(std::move(channel), dispatcher, std::move(on_unbound)) {
    ASSERT_OK(Bind());
  }

  void PrepareAsyncTxn(ResponseContext* context) {
    ClientBase::PrepareAsyncTxn(context);
    std::unique_lock lock(lock_);
    EXPECT_FALSE(txids_.count(context->txid));
    txids_.insert(context->txid);
  }

  void ForgetAsyncTxn(ResponseContext* context) {
    {
      std::unique_lock lock(lock_);
      txids_.erase(context->txid);
    }
    ClientBase::ForgetAsyncTxn(context);
  }

  std::shared_ptr<AsyncBinding> GetBinding() {
    return ClientBase::GetBinding();
  }

  // For responses, find and remove the entry for the matching txid. For events, increment the
  // event count.
  void Dispatch(fidl_msg_t* msg, ResponseContext* context) override {
    auto* hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
    ASSERT_EQ(!hdr->txid, !context);  // hdr->txid == 0 iff context == nullptr.
    std::unique_lock lock(lock_);
    if (hdr->txid) {
      auto txid_it = txids_.find(hdr->txid);
      ASSERT_TRUE(txid_it != txids_.end());  // the transaction must be found.
      txids_.erase(txid_it);
    } else {
      event_count_++;
    }
  }

  uint32_t GetEventCount() {
    std::unique_lock lock(lock_);
    return event_count_;
  }

  bool IsPending(zx_txid_t txid) {
    std::unique_lock lock(lock_);
    return txids_.count(txid);
  }

  size_t GetTxidCount() {
    auto internal_count = list_length(&contexts_.node);
    std::unique_lock lock(lock_);
    EXPECT_EQ(txids_.size(), internal_count);
    return internal_count;
  }

  std::mutex lock_;
  std::unordered_set<zx_txid_t> txids_;
  uint32_t event_count_ = 0;
};

TEST(ClientBaseTestCase, AsyncTxn) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  zx_handle_t local_handle = local.get();

  sync_completion_t unbound;
  TypeErasedOnUnboundFn on_unbound =
      [&](void* impl, UnboundReason reason, zx::channel channel) {
        EXPECT_EQ(fidl::UnboundReason::kPeerClosed, reason);
        EXPECT_EQ(local_handle, channel.get());
        auto* client = static_cast<TestClient*>(impl);
        EXPECT_EQ(0, client->GetTxidCount());
        delete client;
        sync_completion_signal(&unbound);
      };
  auto* client = new TestClient(std::move(local), loop.dispatcher(), std::move(on_unbound));

  // Generate a txid for a ResponseContext. Send a "response" message with the same txid from the
  // remote end of the channel.
  ResponseContext context;
  client->PrepareAsyncTxn(&context);
  EXPECT_TRUE(client->IsPending(context.txid));
  fidl_message_header_t hdr;
  fidl_init_txn_header(&hdr, context.txid, 0);
  ASSERT_OK(remote.write(0, &hdr, sizeof(fidl_message_header_t), nullptr, 0));

  // Trigger unbound handler.
  remote.reset();
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(ClientBaseTestCase, ParallelAsyncTxns) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  zx_handle_t local_handle = local.get();

  sync_completion_t unbound;
  TypeErasedOnUnboundFn on_unbound =
      [&](void* impl, UnboundReason reason, zx::channel channel) {
        EXPECT_EQ(fidl::UnboundReason::kPeerClosed, reason);
        EXPECT_EQ(local_handle, channel.get());
        auto* client = static_cast<TestClient*>(impl);
        EXPECT_EQ(0, client->GetTxidCount());
        delete client;
        sync_completion_signal(&unbound);
      };
  auto* client = new TestClient(std::move(local), loop.dispatcher(), std::move(on_unbound));

  // In parallel, simulate 10 async transactions and send "response" messages from the remote end of
  // the channel.
  ResponseContext contexts[10];
  std::thread threads[10];
  for (int i = 0; i < 10; ++i) {
    threads[i] = std::thread([context = &contexts[i], &remote, client]{
      client->PrepareAsyncTxn(context);
      EXPECT_TRUE(client->IsPending(context->txid));
      fidl_message_header_t hdr;
      fidl_init_txn_header(&hdr, context->txid, 0);
      ASSERT_OK(remote.write(0, &hdr, sizeof(fidl_message_header_t), nullptr, 0));
    });
  }
  for (int i = 0; i < 10; ++i)
    threads[i].join();

  // Trigger unbound handler.
  remote.reset();
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(ClientBaseTestCase, ForgetAsyncTxn) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  TestClient client(std::move(local), loop.dispatcher(), nullptr);

  // Generate a txid for a ResponseContext.
  ResponseContext context;
  client.PrepareAsyncTxn(&context);
  EXPECT_TRUE(client.IsPending(context.txid));

  // Forget the transaction.
  client.ForgetAsyncTxn(&context);
  EXPECT_EQ(0, client.GetTxidCount());
}

TEST(ClientBaseTestCase, UnknownResponseTxid) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  zx_handle_t local_handle = local.get();

  sync_completion_t unbound;
  TypeErasedOnUnboundFn on_unbound =
      [&](void* impl, UnboundReason reason, zx::channel channel) {
        EXPECT_EQ(fidl::UnboundReason::kInternalError, reason);
        EXPECT_EQ(local_handle, channel.get());
        auto* client = static_cast<TestClient*>(impl);
        EXPECT_EQ(0, client->GetTxidCount());
        delete client;
        sync_completion_signal(&unbound);
      };
  auto* client = new TestClient(std::move(local), loop.dispatcher(), std::move(on_unbound));

  // Send a "response" message for which there was no outgoing request.
  ASSERT_EQ(0, client->GetTxidCount());
  fidl_message_header_t hdr;
  fidl_init_txn_header(&hdr, 1, 0);
  ASSERT_OK(remote.write(0, &hdr, sizeof(fidl_message_header_t), nullptr, 0));

  // on_unbound should be triggered by the erroneous response.
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(ClientBaseTestCase, Events) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  zx_handle_t local_handle = local.get();

  sync_completion_t unbound;
  TypeErasedOnUnboundFn on_unbound =
      [&](void* impl, UnboundReason reason, zx::channel channel) {
        EXPECT_EQ(fidl::UnboundReason::kPeerClosed, reason);
        EXPECT_EQ(local_handle, channel.get());
        auto* client = static_cast<TestClient*>(impl);
        EXPECT_EQ(10, client->GetEventCount());  // Expect 10 events.
        delete client;
        sync_completion_signal(&unbound);
      };
  auto* client = new TestClient(std::move(local), loop.dispatcher(), std::move(on_unbound));
  (void)client;

  // In parallel, send 10 event messages from the remote end of the channel.
  std::thread threads[10];
  for (int i = 0; i < 10; ++i) {
    threads[i] = std::thread([&remote]{
      fidl_message_header_t hdr;
      fidl_init_txn_header(&hdr, 0, 0);
      ASSERT_OK(remote.write(0, &hdr, sizeof(fidl_message_header_t), nullptr, 0));
    });
  }
  for (int i = 0; i < 10; ++i)
    threads[i].join();

  // Trigger unbound handler.
  remote.reset();
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(ClientBaseTestCase, Unbind) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  zx_handle_t local_handle = local.get();

  sync_completion_t unbound;
  TypeErasedOnUnboundFn on_unbound = [&](void*, UnboundReason reason, zx::channel channel) {
                                        EXPECT_EQ(fidl::UnboundReason::kUnbind, reason);
                                        EXPECT_EQ(local_handle, channel.get());
                                        sync_completion_signal(&unbound);
                                      };
  auto* client = new TestClient(std::move(local), loop.dispatcher(), std::move(on_unbound));

  // Unbind the client and wait for on_unbound to run.
  client->Unbind();
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(ClientBaseTestCase, UnbindOnDestroy) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  zx_handle_t local_handle = local.get();

  sync_completion_t unbound;
  TypeErasedOnUnboundFn on_unbound = [&](void*, UnboundReason reason, zx::channel channel) {
                                        EXPECT_EQ(fidl::UnboundReason::kUnbind, reason);
                                        EXPECT_EQ(local_handle, channel.get());
                                        sync_completion_signal(&unbound);
                                      };
  auto* client = new TestClient(std::move(local), loop.dispatcher(), std::move(on_unbound));

  // Delete the client and wait for on_unbound to run.
  delete client;
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(ClientBaseTestCase, BindingRefPreventsUnbind) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  zx_handle_t local_handle = local.get();

  sync_completion_t unbound;
  TypeErasedOnUnboundFn on_unbound = [&](void*, UnboundReason reason, zx::channel channel) {
                                        EXPECT_EQ(fidl::UnboundReason::kUnbind, reason);
                                        EXPECT_EQ(local_handle, channel.get());
                                        sync_completion_signal(&unbound);
                                      };
  auto* client = new TestClient(std::move(local), loop.dispatcher(), std::move(on_unbound));

  // Create a strong reference to the binding. Spawn a thread to trigger an Unbind().
  auto binding = client->GetBinding();
  std::thread([client] { client->Unbind(); }).detach();

  // Yield to allow the other thread to run.
  zx_nanosleep(0);

  // unbound should not be signaled until the strong reference is released.
  ASSERT_EQ(ZX_ERR_TIMED_OUT, sync_completion_wait(&unbound, 0));
  binding.reset();
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

}  // namespace
}  // namespace internal
}  // namespace fidl
