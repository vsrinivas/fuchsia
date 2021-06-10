// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/epitaph.h>
#include <lib/fidl/llcpp/client.h>
#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/llcpp/connect_service.h>
#include <lib/fidl/txn_header.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>

#include <mutex>
#include <thread>
#include <vector>

#include <zxtest/zxtest.h>

#include "mock_client_impl.h"

namespace fidl {
namespace {

using ::fidl_testing::TestProtocol;
using ::fidl_testing::TestResponseContext;

// Cloned clients should operate on the same |ClientImpl|.
TEST(WireSharedClient, Clone) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());

  sync_completion_t unbound;
  WireSharedClient<TestProtocol> client;

  class EventHandler : public fidl::WireAsyncEventHandler<TestProtocol> {
   public:
    EventHandler(sync_completion_t& unbound, WireSharedClient<TestProtocol>& client)
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
    WireSharedClient<TestProtocol>& client_;
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
// - Clone a |fidl::WireSharedClient| to another |fidl::WireSharedClient| variable.
// - Destroy the original by letting it go out of scope.
// - Verify that the new client shares the same internal |ClientImpl|.
TEST(WireSharedClient, CloneCanExtendClientLifetime) {
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
    fidl::WireSharedClient<TestProtocol> outer_clone;
    ASSERT_NULL(outer_clone.operator->());

    {
      fidl::WireSharedClient<TestProtocol> inner_clone;
      ASSERT_NULL(inner_clone.operator->());

      {
        fidl::WireSharedClient client(std::move(endpoints->client), loop.dispatcher(),
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

// Calling |Unbind| explicitly will cause all clones to unbind.
TEST(WireSharedClient, CloneSupportsExplicitUnbind) {
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

  fidl::WireSharedClient client(std::move(endpoints->client), loop.dispatcher(),
                                std::make_shared<EventHandler>(did_unbind));
  fidl::WireSharedClient<TestProtocol> clone = client.Clone();

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

// Calling |WaitForChannel| will cause all clones to unbind. The caller of
// |WaitForChannel| will be able to recover the channel.
TEST(WireSharedClient, CloneSupportsWaitForChannel) {
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

  fidl::WireSharedClient client(std::move(endpoints->client), loop.dispatcher(),
                                std::make_shared<EventHandler>(did_unbind));
  fidl::WireSharedClient<TestProtocol> clone = client.Clone();

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

  // Right after |WaitForChannel| returns, we are guaranteed that the
  // |WireSharedClient|s have lost their access to the channel.
  EXPECT_NULL(clone->GetChannel().get());
  EXPECT_NULL(client->GetChannel().get());

  // |did_unbind| is signalled in the |Unbound| handler.
  // It is not required that |WaitForChannel| waits for the execution of
  // the |Unbound| handler, hence the only safe way to check for unbinding
  // is to wait on a |sync_completion_t|, while the event loop thread executes
  // the unbind operation initiated by |WaitForChannel|.
  EXPECT_OK(sync_completion_wait(&did_unbind, zx::duration::infinite().get()));
}

}  // namespace
}  // namespace fidl
