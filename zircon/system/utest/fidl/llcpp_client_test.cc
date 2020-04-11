// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/txn_header.h>
#include <lib/fidl/llcpp/client.h>
#include <lib/fidl/llcpp/client_base.h>
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
namespace {

class TestClient final {
 public:
  // Generated client will define a nested EventHandlers type.
  struct EventHandlers {};

  TestClient(internal::ClientBase* binding, EventHandlers handlers) : binding_(binding) {
    binding_->SetDispatchFn(fit::bind_member(this, &TestClient::Dispatch));
  }

  void PrepareAsyncTxn(internal::ResponseContext* context) {
    binding_->PrepareAsyncTxn(context);
    std::unique_lock lock(lock_);
    EXPECT_FALSE(txids_.count(context->Txid()));
    txids_.insert(context->Txid());
  }

  void ForgetAsyncTxn(internal::ResponseContext* context) {
    {
      std::unique_lock lock(lock_);
      txids_.erase(context->Txid());
    }
    binding_->ForgetAsyncTxn(context);
  }

  std::shared_ptr<internal::AsyncBinding> GetBinding() {
    return binding_->GetBinding();
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
    std::unique_lock lock(lock_);
    EXPECT_EQ(binding_->GetTransactionCount(), txids_.size());
    return txids_.size();
  }

 private:
  // For responses, find and remove the entry for the matching txid. For events, increment the
  // event count.
  zx_status_t Dispatch(fidl_msg_t* msg, internal::ResponseContext* context) {
    auto* hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
    EXPECT_EQ(!hdr->txid, !context);  // hdr->txid == 0 iff context == nullptr.
    if (!hdr->txid != !context) {
      return ZX_OK;  // This is a failure, but let the test continue.
    }
    std::unique_lock lock(lock_);
    if (hdr->txid) {
      auto txid_it = txids_.find(hdr->txid);
      EXPECT_TRUE(txid_it != txids_.end());  // the transaction must be found.
      if (txid_it != txids_.end()) {
        txids_.erase(txid_it);
      }
    } else {
      event_count_++;
    }
    return ZX_OK;
  }

  internal::ClientBase* const binding_;

  std::mutex lock_;
  std::unordered_set<zx_txid_t> txids_;
  uint32_t event_count_ = 0;
};

class TestResponseContext : public internal::ResponseContext {
 public:
  TestResponseContext() = default;
  void OnError() {}
};

TEST(ClientBaseTestCase, AsyncTxn) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  zx_handle_t local_handle = local.get();

  sync_completion_t unbound;
  ClientPtr<TestClient> client;
  OnClientUnboundFn on_unbound = [&](UnboundReason reason, zx::channel channel) {
                                   EXPECT_EQ(fidl::UnboundReason::kPeerClosed, reason);
                                   EXPECT_EQ(local_handle, channel.get());
                                   EXPECT_EQ(0, client->GetTxidCount());
                                   sync_completion_signal(&unbound);
                                 };
  ASSERT_OK(client.Bind(std::move(local), loop.dispatcher(), std::move(on_unbound)));

  // Generate a txid for a ResponseContext. Send a "response" message with the same txid from the
  // remote end of the channel.
  TestResponseContext context;
  client->PrepareAsyncTxn(&context);
  EXPECT_TRUE(client->IsPending(context.Txid()));
  fidl_message_header_t hdr;
  fidl_init_txn_header(&hdr, context.Txid(), 0);
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
  ClientPtr<TestClient> client;
  OnClientUnboundFn on_unbound = [&](UnboundReason reason, zx::channel channel) {
                                   EXPECT_EQ(fidl::UnboundReason::kPeerClosed, reason);
                                   EXPECT_EQ(local_handle, channel.get());
                                   EXPECT_EQ(0, client->GetTxidCount());
                                   sync_completion_signal(&unbound);
                                 };
  ASSERT_OK(client.Bind(std::move(local), loop.dispatcher(), std::move(on_unbound)));

  // In parallel, simulate 10 async transactions and send "response" messages from the remote end of
  // the channel.
  TestResponseContext contexts[10];
  std::thread threads[10];
  for (int i = 0; i < 10; ++i) {
    threads[i] = std::thread([context = &contexts[i], &remote, &client]{
      client->PrepareAsyncTxn(context);
      EXPECT_TRUE(client->IsPending(context->Txid()));
      fidl_message_header_t hdr;
      fidl_init_txn_header(&hdr, context->Txid(), 0);
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

  ClientPtr<TestClient> client(std::move(local), loop.dispatcher());

  // Generate a txid for a ResponseContext.
  TestResponseContext context;
  client->PrepareAsyncTxn(&context);
  EXPECT_TRUE(client->IsPending(context.Txid()));

  // Forget the transaction.
  client->ForgetAsyncTxn(&context);
  EXPECT_EQ(0, client->GetTxidCount());
}

TEST(ClientBaseTestCase, UnknownResponseTxid) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  zx_handle_t local_handle = local.get();

  sync_completion_t unbound;
  ClientPtr<TestClient> client;
  OnClientUnboundFn on_unbound = [&](UnboundReason reason, zx::channel channel) {
                                   EXPECT_EQ(fidl::UnboundReason::kInternalError, reason);
                                   EXPECT_EQ(local_handle, channel.get());
                                   EXPECT_EQ(0, client->GetTxidCount());
                                   sync_completion_signal(&unbound);
                                 };
  ASSERT_OK(client.Bind(std::move(local), loop.dispatcher(), std::move(on_unbound)));

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
  ClientPtr<TestClient> client;
  OnClientUnboundFn on_unbound = [&](UnboundReason reason, zx::channel channel) {
                                   EXPECT_EQ(fidl::UnboundReason::kPeerClosed, reason);
                                   EXPECT_EQ(local_handle, channel.get());
                                   EXPECT_EQ(10, client->GetEventCount());  // Expect 10 events.
                                   sync_completion_signal(&unbound);
                                 };
  ASSERT_OK(client.Bind(std::move(local), loop.dispatcher(), std::move(on_unbound)));

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
  OnClientUnboundFn on_unbound = [&](UnboundReason reason, zx::channel channel) {
                                   EXPECT_EQ(fidl::UnboundReason::kUnbind, reason);
                                   EXPECT_EQ(local_handle, channel.get());
                                   sync_completion_signal(&unbound);
                                 };
  ClientPtr<TestClient> client(std::move(local), loop.dispatcher(), std::move(on_unbound));

  // Unbind the client and wait for on_unbound to run.
  client.Unbind();
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(ClientBaseTestCase, UnbindOnDestroy) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  zx_handle_t local_handle = local.get();

  sync_completion_t unbound;
  OnClientUnboundFn on_unbound = [&](UnboundReason reason, zx::channel channel) {
                                   EXPECT_EQ(fidl::UnboundReason::kUnbind, reason);
                                   EXPECT_EQ(local_handle, channel.get());
                                   sync_completion_signal(&unbound);
                                 };
  auto* client =
      new ClientPtr<TestClient>(std::move(local), loop.dispatcher(), std::move(on_unbound));

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
  OnClientUnboundFn on_unbound = [&](UnboundReason reason, zx::channel channel) {
                                   EXPECT_EQ(fidl::UnboundReason::kUnbind, reason);
                                   EXPECT_EQ(local_handle, channel.get());
                                   sync_completion_signal(&unbound);
                                 };
  ClientPtr<TestClient> client(std::move(local), loop.dispatcher(), std::move(on_unbound));

  // Create a strong reference to the binding. Spawn a thread to trigger an Unbind().
  auto binding = client->GetBinding();
  std::thread([&client] { client.Unbind(); }).detach();

  // Yield to allow the other thread to run.
  zx_nanosleep(0);

  // unbound should not be signaled until the strong reference is released.
  ASSERT_EQ(ZX_ERR_TIMED_OUT, sync_completion_wait(&unbound, 0));
  binding.reset();
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(ClientBaseTestCase, ReleaseOutstandingTxnsOnDestroy) {
  class ReleaseTestResponseContext : public internal::ResponseContext {
   public:
    ReleaseTestResponseContext(sync_completion_t* done) : done_(done) {}
    void OnError() {
      sync_completion_signal(done_);
      delete this;
    }
    sync_completion_t* done_;
  };

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  auto* client = new ClientPtr<TestClient>(std::move(local), loop.dispatcher());

  // Create and register a response context which will signal when deleted.
  sync_completion_t done;
  (*client)->PrepareAsyncTxn(new ReleaseTestResponseContext(&done));

  // Delete the client and ensure that the response context is deleted.
  delete client;
  EXPECT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));
}

}  // namespace
}  // namespace fidl
