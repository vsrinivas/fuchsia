// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/epitaph.h>
#include <lib/fidl/llcpp/client.h>
#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/connect_service.h>
#include <lib/fidl/txn_header.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

#include <zxtest/zxtest.h>

namespace fidl {
namespace {

class TestProtocol {
  TestProtocol() = delete;
};
}  // namespace
}  // namespace fidl
template <>
class ::fidl::WireAsyncEventHandler<fidl::TestProtocol> : public fidl::internal::AsyncEventHandler {
 public:
  WireAsyncEventHandler() = default;
  ~WireAsyncEventHandler() override = default;

  void Unbound(::fidl::UnbindInfo info) override {}
};

template <>
class ::fidl::internal::WireClientImpl<fidl::TestProtocol> : private fidl::internal::ClientBase {
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

  std::shared_ptr<internal::ChannelRef> GetChannel() { return internal::ClientBase::GetChannel(); }

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

  WireClientImpl() = default;

  // For each event, increment the event count.
  std::optional<UnbindInfo> DispatchEvent(fidl::IncomingMessage& msg,
                                          AsyncEventHandler* event_handler) override {
    event_count_++;
    return {};
  }

  std::mutex lock_;
  std::unordered_set<zx_txid_t> txids_;
  uint32_t event_count_ = 0;
};

namespace fidl {
namespace {

class TestResponseContext : public internal::ResponseContext {
 public:
  explicit TestResponseContext(fidl::internal::WireClientImpl<TestProtocol>* client)
      : internal::ResponseContext(0), client_(client) {}
  zx_status_t OnRawReply(fidl::IncomingMessage&& msg) override {
    client_->EraseTxid(this);
    return ZX_OK;
  }
  void OnError() override {}

 private:
  fidl::internal::WireClientImpl<TestProtocol>* client_;
};

TEST(ClientBindingTestCase, AsyncTxn) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t unbound;
  Client<TestProtocol> client;

  class EventHandler : public fidl::WireAsyncEventHandler<TestProtocol> {
   public:
    EventHandler(sync_completion_t& unbound, Client<TestProtocol>& client)
        : unbound_(unbound), client_(client) {}

    void Unbound(::fidl::UnbindInfo info) override {
      EXPECT_EQ(fidl::Reason::kPeerClosed, info.reason());
      EXPECT_EQ(ZX_ERR_PEER_CLOSED, info.status());
      EXPECT_EQ("FIDL endpoint was unbound due to peer closed, status: ZX_ERR_PEER_CLOSED (-24)",
                info.FormatDescription());
      EXPECT_EQ(0, client_->GetTxidCount());
      sync_completion_signal(&unbound_);
    }

   private:
    sync_completion_t& unbound_;
    Client<TestProtocol>& client_;
  };

  client.Bind(std::move(local), loop.dispatcher(), std::make_shared<EventHandler>(unbound, client));

  // Generate a txid for a ResponseContext. Send a "response" message with the same txid from the
  // remote end of the channel.
  TestResponseContext context(client.operator->());
  client->PrepareAsyncTxn(&context);
  EXPECT_TRUE(client->IsPending(context.Txid()));
  fidl_message_header_t hdr;
  fidl_init_txn_header(&hdr, context.Txid(), 0);
  ASSERT_OK(remote.channel().write(0, &hdr, sizeof(fidl_message_header_t), nullptr, 0));

  // Trigger unbound handler.
  remote.reset();
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(ClientBindingTestCase, ParallelAsyncTxns) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t unbound;
  Client<TestProtocol> client;

  class EventHandler : public fidl::WireAsyncEventHandler<TestProtocol> {
   public:
    EventHandler(sync_completion_t& unbound, Client<TestProtocol>& client)
        : unbound_(unbound), client_(client) {}

    void Unbound(::fidl::UnbindInfo info) override {
      EXPECT_EQ(fidl::Reason::kPeerClosed, info.reason());
      EXPECT_EQ(ZX_ERR_PEER_CLOSED, info.status());
      EXPECT_EQ(0, client_->GetTxidCount());
      sync_completion_signal(&unbound_);
    }

   private:
    sync_completion_t& unbound_;
    Client<TestProtocol>& client_;
  };

  client.Bind(std::move(local), loop.dispatcher(), std::make_shared<EventHandler>(unbound, client));

  // In parallel, simulate 10 async transactions and send "response" messages from the remote end of
  // the channel.
  std::vector<std::unique_ptr<TestResponseContext>> contexts;
  std::thread threads[10];
  for (int i = 0; i < 10; ++i) {
    contexts.emplace_back(std::make_unique<TestResponseContext>(client.operator->()));
    threads[i] = std::thread([context = contexts[i].get(), remote = &remote.channel(), &client] {
      client->PrepareAsyncTxn(context);
      EXPECT_TRUE(client->IsPending(context->Txid()));
      fidl_message_header_t hdr;
      fidl_init_txn_header(&hdr, context->Txid(), 0);
      ASSERT_OK(remote->write(0, &hdr, sizeof(fidl_message_header_t), nullptr, 0));
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

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  Client<TestProtocol> client(std::move(local), loop.dispatcher());

  // Generate a txid for a ResponseContext.
  TestResponseContext context(client.operator->());
  client->PrepareAsyncTxn(&context);
  EXPECT_TRUE(client->IsPending(context.Txid()));

  // Forget the transaction.
  client->ForgetAsyncTxn(&context);
  EXPECT_EQ(0, client->GetTxidCount());
}

TEST(ClientBindingTestCase, UnknownResponseTxid) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t unbound;
  Client<TestProtocol> client;

  class EventHandler : public fidl::WireAsyncEventHandler<TestProtocol> {
   public:
    EventHandler(sync_completion_t& unbound, Client<TestProtocol>& client)
        : unbound_(unbound), client_(client) {}

    void Unbound(::fidl::UnbindInfo info) override {
      EXPECT_EQ(fidl::Reason::kUnexpectedMessage, info.reason());
      EXPECT_EQ(ZX_ERR_NOT_FOUND, info.status());
      EXPECT_EQ(
          "FIDL endpoint was unbound due to unexpected message, "
          "status: ZX_ERR_NOT_FOUND (-25), detail: unknown txid",
          info.FormatDescription());
      EXPECT_EQ(0, client_->GetTxidCount());
      sync_completion_signal(&unbound_);
    }

   private:
    sync_completion_t& unbound_;
    Client<TestProtocol>& client_;
  };

  client.Bind(std::move(local), loop.dispatcher(), std::make_shared<EventHandler>(unbound, client));

  // Send a "response" message for which there was no outgoing request.
  ASSERT_EQ(0, client->GetTxidCount());
  fidl_message_header_t hdr;
  fidl_init_txn_header(&hdr, 1, 0);
  ASSERT_OK(remote.channel().write(0, &hdr, sizeof(fidl_message_header_t), nullptr, 0));

  // on_unbound should be triggered by the erroneous response.
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(ClientBindingTestCase, Events) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t unbound;
  Client<TestProtocol> client;

  class EventHandler : public fidl::WireAsyncEventHandler<TestProtocol> {
   public:
    EventHandler(sync_completion_t& unbound, Client<TestProtocol>& client)
        : unbound_(unbound), client_(client) {}

    void Unbound(::fidl::UnbindInfo info) override {
      EXPECT_EQ(fidl::Reason::kPeerClosed, info.reason());
      EXPECT_EQ(ZX_ERR_PEER_CLOSED, info.status());
      EXPECT_EQ(10, client_->GetEventCount());  // Expect 10 events.
      sync_completion_signal(&unbound_);
    }

   private:
    sync_completion_t& unbound_;
    Client<TestProtocol>& client_;
  };

  client.Bind(std::move(local), loop.dispatcher(), std::make_shared<EventHandler>(unbound, client));

  // In parallel, send 10 event messages from the remote end of the channel.
  std::thread threads[10];
  for (int i = 0; i < 10; ++i) {
    threads[i] = std::thread([remote = &remote.channel()] {
      fidl_message_header_t hdr;
      fidl_init_txn_header(&hdr, 0, 0);
      ASSERT_OK(remote->write(0, &hdr, sizeof(fidl_message_header_t), nullptr, 0));
    });
  }
  for (int i = 0; i < 10; ++i)
    threads[i].join();

  // Trigger unbound handler.
  remote.reset();
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(ClientBindingTestCase, UnbindOnInvalidClientShouldPanic) {
  Client<TestProtocol> client;
  ASSERT_DEATH([&] { client.Unbind(); });
}

TEST(ClientBindingTestCase, Unbind) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t unbound;

  class EventHandler : public fidl::WireAsyncEventHandler<TestProtocol> {
   public:
    explicit EventHandler(sync_completion_t& unbound) : unbound_(unbound) {}

    void Unbound(::fidl::UnbindInfo info) override {
      EXPECT_EQ(fidl::Reason::kUnbind, info.reason());
      EXPECT_OK(info.status());
      sync_completion_signal(&unbound_);
    }

   private:
    sync_completion_t& unbound_;
  };

  Client<TestProtocol> client(std::move(local), loop.dispatcher(),
                              std::make_shared<EventHandler>(unbound));

  // Unbind the client and wait for on_unbound to run.
  client.Unbind();
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(ClientBindingTestCase, UnbindOnDestroy) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t unbound;

  class EventHandler : public fidl::WireAsyncEventHandler<TestProtocol> {
   public:
    explicit EventHandler(sync_completion_t& unbound) : unbound_(unbound) {}

    void Unbound(::fidl::UnbindInfo info) override {
      EXPECT_EQ(fidl::Reason::kUnbind, info.reason());
      EXPECT_OK(info.status());
      sync_completion_signal(&unbound_);
    }

   private:
    sync_completion_t& unbound_;
  };

  auto* client = new Client<TestProtocol>(std::move(local), loop.dispatcher(),
                                          std::make_shared<EventHandler>(unbound));

  // Delete the client and wait for on_unbound to run.
  delete client;
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(ClientBindingTestCase, UnbindWhileActiveChannelRefs) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t unbound;

  class EventHandler : public fidl::WireAsyncEventHandler<TestProtocol> {
   public:
    explicit EventHandler(sync_completion_t& unbound) : unbound_(unbound) {}

    void Unbound(::fidl::UnbindInfo info) override {
      EXPECT_EQ(fidl::Reason::kUnbind, info.reason());
      EXPECT_OK(info.status());
      sync_completion_signal(&unbound_);
    }

   private:
    sync_completion_t& unbound_;
  };

  Client<TestProtocol> client(std::move(local), loop.dispatcher(),
                              std::make_shared<EventHandler>(unbound));

  // Create a strong reference to the channel.
  auto channel = client->GetChannel();

  // Unbind() and the unbound handler should not be blocked by the channel reference.
  client.Unbind();
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));

  // Check that the channel handle is still valid.
  EXPECT_OK(
      zx_object_get_info(channel->handle(), ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr));
}

// Cloned clients should operate on the same |ClientImpl|.
TEST(ClientBindingTestCase, Clone) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());

  sync_completion_t unbound;
  Client<TestProtocol> client;

  class EventHandler : public fidl::WireAsyncEventHandler<TestProtocol> {
   public:
    EventHandler(sync_completion_t& unbound, Client<TestProtocol>& client)
        : unbound_(unbound), client_(client) {}

    void Unbound(::fidl::UnbindInfo info) override {
      EXPECT_EQ(fidl::Reason::kPeerClosed, info.reason());
      EXPECT_EQ(ZX_ERR_PEER_CLOSED, info.status());
      // All the transactions should be finished by the time the connection is dropped.
      EXPECT_EQ(0, client_->GetTxidCount());
      sync_completion_signal(&unbound_);
    }

   private:
    sync_completion_t& unbound_;
    Client<TestProtocol>& client_;
  };

  client.Bind(std::move(endpoints->client), loop.dispatcher(),
              std::make_shared<EventHandler>(unbound, client));

  // Create 20 clones of the client, and verify that they can all send messages
  // through the same internal |ClientImpl|.
  constexpr size_t kNumClones = 20;
  std::vector<std::unique_ptr<TestResponseContext>> contexts;
  for (size_t i = 0; i < kNumClones; i++) {
    auto clone = client.Clone();
    contexts.emplace_back(std::make_unique<TestResponseContext>(clone.operator->()));
    // Generate a txid for a ResponseContext.
    clone->PrepareAsyncTxn(contexts.back().get());
    // Both clone and the client should delegate to the same underlying binding.
    EXPECT_TRUE(clone->IsPending(contexts.back()->Txid()));
    EXPECT_TRUE(client->IsPending(contexts.back()->Txid()));
    // Send a "response" message with the same txid from the remote end of the channel.
    fidl_message_header_t hdr;
    fidl_init_txn_header(&hdr, contexts.back()->Txid(), 0);
    ASSERT_OK(
        endpoints->server.channel().write(0, &hdr, sizeof(fidl_message_header_t), nullptr, 0));
  }

  // Trigger unbound handler.
  endpoints->server.channel().reset();
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

// This test performs the following repeatedly:
// - Clone a |fidl::Client| to another |fidl::Client| variable.
// - Destroy the original by letting it go out of scope.
// - Verify that the new client shares the same internal |ClientImpl|.
TEST(ClientBindingTestCase, CloneCanExtendClientLifetime) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());

  bool did_unbind = false;
  class EventHandler : public fidl::WireAsyncEventHandler<TestProtocol> {
   public:
    explicit EventHandler(bool& did_unbind) : did_unbind_(did_unbind) {}

    void Unbound(::fidl::UnbindInfo info) override {
      // The reason should be |kUnbind| because |outer_clone| going out of
      // scope will trigger unbinding.
      EXPECT_EQ(fidl::Reason::kUnbind, info.reason());
      EXPECT_EQ(ZX_OK, info.status());
      did_unbind_ = true;
    }

   private:
    bool& did_unbind_;
  };

  {
    fidl::internal::WireClientImpl<TestProtocol>* client_ptr = nullptr;
    fidl::Client<TestProtocol> outer_clone;
    ASSERT_NULL(outer_clone.operator->());

    {
      fidl::Client<TestProtocol> inner_clone;
      ASSERT_NULL(inner_clone.operator->());

      {
        fidl::Client client(std::move(endpoints->client), loop.dispatcher(),
                            std::make_shared<EventHandler>(did_unbind));
        ASSERT_NOT_NULL(client.operator->());
        client_ptr = &*client;

        ASSERT_OK(loop.RunUntilIdle());
        ASSERT_FALSE(did_unbind);

        // Extend the client lifetime to |inner_clone|.
        inner_clone = client.Clone();
      }

      ASSERT_NOT_NULL(inner_clone.operator->());
      ASSERT_EQ(&*inner_clone, client_ptr);

      ASSERT_OK(loop.RunUntilIdle());
      ASSERT_FALSE(did_unbind);

      // Extend the client lifetime to |outer_clone|.
      outer_clone = inner_clone.Clone();
    }

    ASSERT_NOT_NULL(outer_clone.operator->());
    ASSERT_EQ(&*outer_clone, client_ptr);

    ASSERT_OK(loop.RunUntilIdle());
    ASSERT_FALSE(did_unbind);
  }

  // Verify that unbinding still happens when all the clients
  // referencing the same connection go out of scope.
  ASSERT_OK(loop.RunUntilIdle());
  ASSERT_TRUE(did_unbind);
}

TEST(ClientBindingTestCase, CloneSupportsExplicitUnbind) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());

  bool did_unbind = false;
  class EventHandler : public fidl::WireAsyncEventHandler<TestProtocol> {
   public:
    explicit EventHandler(bool& did_unbind) : did_unbind_(did_unbind) {}

    void Unbound(::fidl::UnbindInfo info) override {
      // The reason should be |kUnbind| because we are explicitly calling |Unbind|.
      EXPECT_EQ(fidl::Reason::kUnbind, info.reason());
      EXPECT_EQ(ZX_OK, info.status());
      did_unbind_ = true;
    }

   private:
    bool& did_unbind_;
  };

  fidl::Client client(std::move(endpoints->client), loop.dispatcher(),
                      std::make_shared<EventHandler>(did_unbind));
  fidl::Client<TestProtocol> clone = client.Clone();

  ASSERT_OK(loop.RunUntilIdle());
  ASSERT_FALSE(did_unbind);

  // The channel being managed is still alive.
  ASSERT_NOT_NULL(clone->GetChannel().get());

  // Now we call |Unbind| on the main client, the clone would be unbound too.
  client.Unbind();

  ASSERT_OK(loop.RunUntilIdle());
  EXPECT_TRUE(did_unbind);
  EXPECT_NULL(clone->GetChannel().get());
  EXPECT_NULL(client->GetChannel().get());
}

TEST(ClientBindingTestCase, CloneSupportsWaitForChannel) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());

  sync_completion_t did_unbind;
  class EventHandler : public fidl::WireAsyncEventHandler<TestProtocol> {
   public:
    explicit EventHandler(sync_completion_t& did_unbind) : did_unbind_(did_unbind) {}

    void Unbound(::fidl::UnbindInfo info) override {
      // The reason should be |kUnbind| because we are calling |WaitForChannel|
      // which triggers unbinding.
      EXPECT_EQ(fidl::Reason::kUnbind, info.reason());
      EXPECT_EQ(ZX_OK, info.status());
      sync_completion_signal(&did_unbind_);
    }

   private:
    sync_completion_t& did_unbind_;
  };

  fidl::Client client(std::move(endpoints->client), loop.dispatcher(),
                      std::make_shared<EventHandler>(did_unbind));
  fidl::Client<TestProtocol> clone = client.Clone();

  ASSERT_OK(loop.RunUntilIdle());
  ASSERT_EQ(ZX_ERR_TIMED_OUT,
            sync_completion_wait(&did_unbind, zx::duration::infinite_past().get()));

  // The channel being managed is still alive.
  ASSERT_NOT_NULL(clone->GetChannel().get());

  // Now we call |WaitForChannel| on the main client, the clone would be unbound too.
  // Note that |WaitForChannel| itself is blocking, so we cannot block the async loop
  // at the same time.
  ASSERT_OK(loop.StartThread());
  auto client_end = client.WaitForChannel();
  EXPECT_TRUE(client_end.is_valid());

  // Right after |WaitForChannel| returns, we are guaranteed that the |Client|s
  // have lost their access to the channel.
  EXPECT_NULL(clone->GetChannel().get());
  EXPECT_NULL(client->GetChannel().get());

  // |did_unbind| is signalled in the |Unbound| handler.
  // It is not required that |WaitForChannel| waits for the execution of
  // the |Unbound| handler, hence the only safe way to check for unbinding
  // is to wait on a |sync_completion_t|, while the event loop thread executes
  // the unbind operation initiated by |WaitForChannel|.
  EXPECT_OK(sync_completion_wait(&did_unbind, zx::duration::infinite().get()));
}

class ReleaseTestResponseContext : public internal::ResponseContext {
 public:
  explicit ReleaseTestResponseContext(sync_completion_t* done)
      : internal::ResponseContext(0), done_(done) {}
  zx_status_t OnRawReply(fidl::IncomingMessage&& msg) override {
    delete this;
    return ZX_OK;
  }
  void OnError() override {
    sync_completion_signal(done_);
    delete this;
  }
  sync_completion_t* done_;
};

TEST(ClientBindingTestCase, ReleaseOutstandingTxnsOnDestroy) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

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

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

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

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t unbound;

  class EventHandler : public fidl::WireAsyncEventHandler<TestProtocol> {
   public:
    explicit EventHandler(sync_completion_t& unbound) : unbound_(unbound) {}

    void Unbound(::fidl::UnbindInfo info) override {
      EXPECT_EQ(fidl::Reason::kPeerClosed, info.reason());
      EXPECT_EQ(ZX_ERR_BAD_STATE, info.status());
      sync_completion_signal(&unbound_);
    }

   private:
    sync_completion_t& unbound_;
  };

  Client<TestProtocol> client(std::move(local), loop.dispatcher(),
                              std::make_shared<EventHandler>(unbound));

  // Send an epitaph and wait for on_unbound to run.
  ASSERT_OK(fidl_epitaph_write(remote.channel().get(), ZX_ERR_BAD_STATE));
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(ClientBindingTestCase, PeerClosedNoEpitaph) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t unbound;

  class EventHandler : public fidl::WireAsyncEventHandler<TestProtocol> {
   public:
    explicit EventHandler(sync_completion_t& unbound) : unbound_(unbound) {}

    void Unbound(::fidl::UnbindInfo info) override {
      EXPECT_EQ(fidl::Reason::kPeerClosed, info.reason());
      // No epitaph is equivalent to ZX_ERR_PEER_CLOSED epitaph.
      EXPECT_EQ(ZX_ERR_PEER_CLOSED, info.status());
      sync_completion_signal(&unbound_);
    }

   private:
    sync_completion_t& unbound_;
  };

  Client<TestProtocol> client(std::move(local), loop.dispatcher(),
                              std::make_shared<EventHandler>(unbound));

  // Close the server end and wait for on_unbound to run.
  remote.reset();
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(ChannelRefTrackerTestCase, NoWaitNoHandleLeak) {
  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  // Pass ownership of local end of the channel to the ChannelRefTracker.
  auto channel_tracker = new internal::ChannelRefTracker();
  channel_tracker->Init(std::move(local));

  // Destroy the ChannelRefTracker. ZX_SIGNAL_PEER_CLOSED should be asserted on remote.
  delete channel_tracker;
  EXPECT_OK(remote.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite_past(), nullptr));
}

TEST(ChannelRefTrackerTestCase, WaitForChannelWithoutRefs) {
  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  auto local_handle = local.get();

  // Pass ownership of local end of the channel to the ChannelRefTracker.
  internal::ChannelRefTracker channel_tracker;
  channel_tracker.Init(std::move(local));

  // Retrieve the channel. Check the validity of the handle.
  local = channel_tracker.WaitForChannel();
  ASSERT_EQ(local_handle, local.get());
  ASSERT_OK(local.get_info(ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr));

  // Ensure that no new references can be created.
  EXPECT_FALSE(channel_tracker.Get());
}

TEST(ChannelRefTrackerTestCase, WaitForChannelWithRefs) {
  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  auto local_handle = local.get();

  // Pass ownership of local end of the channel to the ChannelRefTracker.
  internal::ChannelRefTracker channel_tracker;
  channel_tracker.Init(std::move(local));

  // Get a new reference.
  auto channel_ref = channel_tracker.Get();
  ASSERT_EQ(local_handle, channel_ref->handle());

  // Pass the reference to another thread, then wait for it to be released.
  // NOTE: This is inherently racy but should never fail regardless of the particular state.
  sync_completion_t running;
  std::thread([&running, channel_ref = std::move(channel_ref)]() mutable {
    sync_completion_signal(&running);  // Let the main thread continue.
    channel_ref = nullptr;             // Release this reference.
  }).detach();

  ASSERT_OK(sync_completion_wait(&running, ZX_TIME_INFINITE));

  // Retrieve the channel. Check the validity of the handle.
  local = channel_tracker.WaitForChannel();
  ASSERT_EQ(local_handle, local.get());
  ASSERT_OK(local.get_info(ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr));

  // Ensure that no new references can be created.
  EXPECT_FALSE(channel_tracker.Get());
}

}  // namespace
}  // namespace fidl
