// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/epitaph.h>
#include <lib/fidl/llcpp/client.h>
#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/txn_header.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <mutex>
#include <thread>
#include <unordered_set>

#include <zxtest/zxtest.h>

namespace fidl {
namespace {

class TestProtocol {
  TestProtocol() = delete;

 public:
  // Generated code will define an AsyncEventHandlers type.
  struct AsyncEventHandlers {};

  class ClientImpl final : private internal::ClientBase {
   public:
    void PrepareAsyncTxn(internal::ResponseContext* context) {
      internal::ClientBase::PrepareAsyncTxn(context);
      std::unique_lock lock(lock_);
      EXPECT_FALSE(txids_.count(context->Txid()));
      txids_.insert(context->Txid());
    }

    void ForgetAsyncTxn(internal::ResponseContext* context) {
      {
        std::unique_lock lock(lock_);
        txids_.erase(context->Txid());
      }
      internal::ClientBase::ForgetAsyncTxn(context);
    }

    void EraseTxid(internal::ResponseContext* context) {
      {
        std::unique_lock lock(lock_);
        txids_.erase(context->Txid());
      }
    }

    std::shared_ptr<internal::AsyncBinding> GetBinding() {
      return internal::ClientBase::GetBinding();
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
      EXPECT_EQ(internal::ClientBase::GetTransactionCount(), txids_.size());
      return txids_.size();
    }

   private:
    friend class Client<TestProtocol>;

    explicit ClientImpl(AsyncEventHandlers handlers) {}

    // For each event, increment the event count.
    std::optional<UnbindInfo> DispatchEvent(fidl_msg_t* msg) {
      event_count_++;
      return {};
    }

    std::mutex lock_;
    std::unordered_set<zx_txid_t> txids_;
    uint32_t event_count_ = 0;
  };
};

class TestResponseContext : public internal::ResponseContext {
 public:
  explicit TestResponseContext(TestProtocol::ClientImpl* client)
      : internal::ResponseContext(&::fidl::_llcpp_coding_AnyZeroArgMessageTable, 0),
        client_(client) {}
  void OnReply(uint8_t* reply) override { client_->EraseTxid(this); }
  void OnError() override {}

 private:
  TestProtocol::ClientImpl* client_;
};

TEST(ClientBindingTestCase, AsyncTxn) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  zx_handle_t local_handle = local.get();

  sync_completion_t unbound;
  Client<TestProtocol> client;
  OnClientUnboundFn on_unbound = [&](UnbindInfo info, zx::channel channel) {
    EXPECT_EQ(fidl::UnbindInfo::kPeerClosed, info.reason);
    EXPECT_EQ(ZX_ERR_PEER_CLOSED, info.status);
    EXPECT_EQ(local_handle, channel.get());
    EXPECT_EQ(0, client->GetTxidCount());
    sync_completion_signal(&unbound);
  };
  ASSERT_OK(client.Bind(std::move(local), loop.dispatcher(), std::move(on_unbound)));

  // Generate a txid for a ResponseContext. Send a "response" message with the same txid from the
  // remote end of the channel.
  TestResponseContext context(client.get());
  client->PrepareAsyncTxn(&context);
  EXPECT_TRUE(client->IsPending(context.Txid()));
  fidl_message_header_t hdr;
  fidl_init_txn_header(&hdr, context.Txid(), 0);
  ASSERT_OK(remote.write(0, &hdr, sizeof(fidl_message_header_t), nullptr, 0));

  // Trigger unbound handler.
  remote.reset();
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(ClientBindingTestCase, ParallelAsyncTxns) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  zx_handle_t local_handle = local.get();

  sync_completion_t unbound;
  Client<TestProtocol> client;
  OnClientUnboundFn on_unbound = [&](UnbindInfo info, zx::channel channel) {
    EXPECT_EQ(fidl::UnbindInfo::kPeerClosed, info.reason);
    EXPECT_EQ(ZX_ERR_PEER_CLOSED, info.status);
    EXPECT_EQ(local_handle, channel.get());
    EXPECT_EQ(0, client->GetTxidCount());
    sync_completion_signal(&unbound);
  };
  ASSERT_OK(client.Bind(std::move(local), loop.dispatcher(), std::move(on_unbound)));

  // In parallel, simulate 10 async transactions and send "response" messages from the remote end of
  // the channel.
  std::vector<std::unique_ptr<TestResponseContext>> contexts;
  std::thread threads[10];
  for (int i = 0; i < 10; ++i) {
    contexts.emplace_back(std::make_unique<TestResponseContext>(client.get()));
    threads[i] = std::thread([context = contexts[i].get(), &remote, &client] {
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

TEST(ClientBindingTestCase, ForgetAsyncTxn) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  Client<TestProtocol> client(std::move(local), loop.dispatcher());

  // Generate a txid for a ResponseContext.
  TestResponseContext context(client.get());
  client->PrepareAsyncTxn(&context);
  EXPECT_TRUE(client->IsPending(context.Txid()));

  // Forget the transaction.
  client->ForgetAsyncTxn(&context);
  EXPECT_EQ(0, client->GetTxidCount());
}

TEST(ClientBindingTestCase, UnknownResponseTxid) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  zx_handle_t local_handle = local.get();

  sync_completion_t unbound;
  Client<TestProtocol> client;
  OnClientUnboundFn on_unbound = [&](UnbindInfo info, zx::channel channel) {
    EXPECT_EQ(fidl::UnbindInfo::kUnexpectedMessage, info.reason);
    EXPECT_EQ(ZX_ERR_NOT_FOUND, info.status);
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

TEST(ClientBindingTestCase, Events) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  zx_handle_t local_handle = local.get();

  sync_completion_t unbound;
  Client<TestProtocol> client;
  OnClientUnboundFn on_unbound = [&](UnbindInfo info, zx::channel channel) {
    EXPECT_EQ(fidl::UnbindInfo::kPeerClosed, info.reason);
    EXPECT_EQ(ZX_ERR_PEER_CLOSED, info.status);
    EXPECT_EQ(local_handle, channel.get());
    EXPECT_EQ(10, client->GetEventCount());  // Expect 10 events.
    sync_completion_signal(&unbound);
  };
  ASSERT_OK(client.Bind(std::move(local), loop.dispatcher(), std::move(on_unbound)));

  // In parallel, send 10 event messages from the remote end of the channel.
  std::thread threads[10];
  for (int i = 0; i < 10; ++i) {
    threads[i] = std::thread([&remote] {
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

TEST(ClientBindingTestCase, Unbind) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  zx_handle_t local_handle = local.get();

  sync_completion_t unbound;
  OnClientUnboundFn on_unbound = [&](UnbindInfo info, zx::channel channel) {
    EXPECT_EQ(fidl::UnbindInfo::kUnbind, info.reason);
    EXPECT_OK(info.status);
    EXPECT_EQ(local_handle, channel.get());
    sync_completion_signal(&unbound);
  };
  Client<TestProtocol> client(std::move(local), loop.dispatcher(), std::move(on_unbound));

  // Unbind the client and wait for on_unbound to run.
  client.Unbind();
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(ClientBindingTestCase, UnbindOnDestroy) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  zx_handle_t local_handle = local.get();

  sync_completion_t unbound;
  OnClientUnboundFn on_unbound = [&](UnbindInfo info, zx::channel channel) {
    EXPECT_EQ(fidl::UnbindInfo::kUnbind, info.reason);
    EXPECT_OK(info.status);
    EXPECT_EQ(local_handle, channel.get());
    sync_completion_signal(&unbound);
  };
  auto* client =
      new Client<TestProtocol>(std::move(local), loop.dispatcher(), std::move(on_unbound));

  // Delete the client and wait for on_unbound to run.
  delete client;
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(ClientBindingTestCase, BindingRefPreventsUnbind) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  zx_handle_t local_handle = local.get();

  sync_completion_t unbound;
  OnClientUnboundFn on_unbound = [&](UnbindInfo info, zx::channel channel) {
    EXPECT_EQ(fidl::UnbindInfo::kUnbind, info.reason);
    EXPECT_OK(info.status);
    EXPECT_EQ(local_handle, channel.get());
    sync_completion_signal(&unbound);
  };
  Client<TestProtocol> client(std::move(local), loop.dispatcher(), std::move(on_unbound));

  // Create a strong reference to the binding.
  auto binding = client->GetBinding();

  client.Unbind();  // Unbind() should not be blocked by the strong reference.

  // unbound should not be signaled until the strong reference is released.
  ASSERT_EQ(ZX_ERR_TIMED_OUT, sync_completion_wait(&unbound, 0));
  binding.reset();
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

class ReleaseTestResponseContext : public internal::ResponseContext {
 public:
  explicit ReleaseTestResponseContext(sync_completion_t* done)
      : internal::ResponseContext(&::fidl::_llcpp_coding_AnyZeroArgMessageTable, 0),
        done_(done) {}
  void OnReply(uint8_t* reply) override { delete this; }
  void OnError() override {
    sync_completion_signal(done_);
    delete this;
  }
  sync_completion_t* done_;
};

TEST(ClientBindingTestCase, ReleaseOutstandingTxnsOnDestroy) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  auto* client = new Client<TestProtocol>(std::move(local), loop.dispatcher());

  // Create and register a response context which will signal when deleted.
  sync_completion_t done;
  (*client)->PrepareAsyncTxn(new ReleaseTestResponseContext(&done));

  // Delete the client and ensure that the response context is deleted.
  delete client;
  EXPECT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));
}

TEST(ClientBindingTestCase, ReleaseOutstandingTxnsOnUnbound) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  Client<TestProtocol> client(std::move(local), loop.dispatcher());

  // Create and register a response context which will signal when deleted.
  sync_completion_t done;
  client->PrepareAsyncTxn(new ReleaseTestResponseContext(&done));

  // Trigger unbinding and wait for the transaction context to be released.
  remote.reset();
  EXPECT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));
}

TEST(ClientBindingTestCase, Epitaph) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  zx_handle_t local_handle = local.get();

  sync_completion_t unbound;
  OnClientUnboundFn on_unbound = [&](UnbindInfo info, zx::channel channel) {
    EXPECT_EQ(fidl::UnbindInfo::kPeerClosed, info.reason);
    EXPECT_EQ(ZX_ERR_BAD_STATE, info.status);
    EXPECT_EQ(local_handle, channel.get());
    sync_completion_signal(&unbound);
  };
  Client<TestProtocol> client(std::move(local), loop.dispatcher(), std::move(on_unbound));

  // Send an epitaph and wait for on_unbound to run.
  ASSERT_OK(fidl_epitaph_write(remote.get(), ZX_ERR_BAD_STATE));
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(ClientBindingTestCase, PeerClosedNoEpitaph) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  zx_handle_t local_handle = local.get();

  sync_completion_t unbound;
  OnClientUnboundFn on_unbound = [&](UnbindInfo info, zx::channel channel) {
    EXPECT_EQ(fidl::UnbindInfo::kPeerClosed, info.reason);
    // No epitaph is equivalent to ZX_ERR_PEER_CLOSED epitaph.
    EXPECT_EQ(ZX_ERR_PEER_CLOSED, info.status);
    EXPECT_EQ(local_handle, channel.get());
    sync_completion_signal(&unbound);
  };
  Client<TestProtocol> client(std::move(local), loop.dispatcher(), std::move(on_unbound));

  // Close the server end and wait for on_unbound to run.
  remote.reset();
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

}  // namespace
}  // namespace fidl
