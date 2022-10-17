// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.test.coding.fuchsia/cpp/wire.h>
#include <fidl/test.basic.protocol/cpp/wire.h>
#include <fidl/test.basic.protocol/cpp/wire_test_base.h>
#include <fidl/test.empty.protocol/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/wire/server.h>
#include <lib/stdcompat/functional.h>
#include <lib/sync/completion.h>
#include <lib/sync/cpp/completion.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>

#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include <zxtest/zxtest.h>

namespace {

using ::test_basic_protocol::Closer;
using ::test_basic_protocol::ValueEcho;
using ::test_basic_protocol::Values;

constexpr uint32_t kNumberOfAsyncs = 10;
constexpr char kExpectedReply[] = "test";

TEST(BindServerTestCase, SyncReply) {
  struct SyncServer : fidl::WireServer<ValueEcho> {
    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      EXPECT_TRUE(completer.is_reply_needed());
      completer.Reply(request->s);
      EXPECT_FALSE(completer.is_reply_needed());
    }
  };

  // Server launches a thread so we can make sync client calls.
  auto server = std::make_unique<SyncServer>();
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<ValueEcho>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t closed;
  fidl::OnUnboundFn<SyncServer> on_unbound = [&closed](SyncServer*, fidl::UnbindInfo info,
                                                       fidl::ServerEnd<ValueEcho> server_end) {
    EXPECT_EQ(fidl::Reason::kPeerClosed, info.reason());
    EXPECT_EQ(ZX_ERR_PEER_CLOSED, info.status());
    EXPECT_TRUE(server_end);
    sync_completion_signal(&closed);
  };
  fidl::BindServer(loop.dispatcher(), std::move(remote), server.get(), std::move(on_unbound));

  // Sync client call.
  auto result = fidl::WireCall(local)->Echo(kExpectedReply);
  EXPECT_OK(result.status());
  EXPECT_EQ(result.value().s.get(), kExpectedReply);

  local.reset();  // To trigger binding destruction before loop's destruction.
  ASSERT_OK(sync_completion_wait(&closed, ZX_TIME_INFINITE));
}

TEST(BindServerTestCase, AsyncReply) {
  struct AsyncServer : fidl::WireServer<ValueEcho> {
    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      worker_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
      async::PostTask(worker_->dispatcher(), [request = std::string(request->s.get()),
                                              completer = completer.ToAsync()]() mutable {
        EXPECT_TRUE(completer.is_reply_needed());
        completer.Reply(fidl::StringView::FromExternal(request));
        EXPECT_FALSE(completer.is_reply_needed());
      });
      EXPECT_FALSE(completer.is_reply_needed());
      ASSERT_OK(worker_->StartThread());
    }
    std::unique_ptr<async::Loop> worker_;
  };

  // Server launches a thread so we can make sync client calls.
  auto server = std::make_unique<AsyncServer>();
  async::Loop main(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(main.StartThread());

  auto endpoints = fidl::CreateEndpoints<ValueEcho>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t closed;
  fidl::OnUnboundFn<AsyncServer> on_unbound = [&closed](AsyncServer*, fidl::UnbindInfo info,
                                                        fidl::ServerEnd<ValueEcho> server_end) {
    EXPECT_EQ(fidl::Reason::kPeerClosed, info.reason());
    EXPECT_EQ(ZX_ERR_PEER_CLOSED, info.status());
    EXPECT_TRUE(server_end);
    sync_completion_signal(&closed);
  };
  fidl::BindServer(main.dispatcher(), std::move(remote), server.get(), std::move(on_unbound));

  // Sync client call.
  auto result = fidl::WireCall(local)->Echo(kExpectedReply);
  EXPECT_OK(result.status());
  EXPECT_EQ(result.value().s.get(), kExpectedReply);

  local.reset();  // To trigger binding destruction before main's destruction.
  ASSERT_OK(sync_completion_wait(&closed, ZX_TIME_INFINITE));
}

TEST(BindServerTestCase, MultipleAsyncReplies) {
  struct AsyncDelayedServer : fidl::WireServer<ValueEcho> {
    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      auto worker = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
      async::PostTask(worker->dispatcher(), [request = std::string(request->s.get()),
                                             completer = completer.ToAsync(), this]() mutable {
        static std::atomic<int> count;
        // Since we block until we get kNumberOfAsyncs concurrent requests
        // this can only pass if we allow concurrent async replies.
        if (++count == kNumberOfAsyncs) {
          sync_completion_signal(&done_);
        }
        sync_completion_wait(&done_, ZX_TIME_INFINITE);
        completer.Reply(fidl::StringView::FromExternal(request));
      });
      ASSERT_OK(worker->StartThread());
      loops_.push_back(std::move(worker));
    }
    sync_completion_t done_;
    std::vector<std::unique_ptr<async::Loop>> loops_;
  };

  // Server launches a thread so we can make sync client calls.
  auto server = std::make_unique<AsyncDelayedServer>();
  async::Loop main(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(main.StartThread());

  auto endpoints = fidl::CreateEndpoints<ValueEcho>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t closed;
  fidl::OnUnboundFn<AsyncDelayedServer> on_unbound =
      [&closed](AsyncDelayedServer* server, fidl::UnbindInfo info,
                fidl::ServerEnd<ValueEcho> server_end) {
        EXPECT_EQ(fidl::Reason::kPeerClosed, info.reason());
        EXPECT_EQ(ZX_ERR_PEER_CLOSED, info.status());
        EXPECT_TRUE(server_end);
        sync_completion_signal(&closed);
      };
  fidl::BindServer(main.dispatcher(), std::move(remote), server.get(), std::move(on_unbound));

  // Sync client calls.
  sync_completion_t done;
  std::vector<std::unique_ptr<async::Loop>> clients;
  for (uint32_t i = 0; i < kNumberOfAsyncs; ++i) {
    auto client = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    async::PostTask(client->dispatcher(), [local = local.borrow(), &done]() {
      auto result = fidl::WireCall(local)->Echo(kExpectedReply);
      ASSERT_EQ(result.value().s.get(), kExpectedReply);
      static std::atomic<int> count;
      if (++count == kNumberOfAsyncs) {
        sync_completion_signal(&done);
      }
    });
    ASSERT_OK(client->StartThread());
    clients.push_back(std::move(client));
  }
  sync_completion_wait(&done, ZX_TIME_INFINITE);

  local.reset();  // To trigger binding destruction before main's destruction.
  ASSERT_OK(sync_completion_wait(&closed, ZX_TIME_INFINITE));
}

// This test races |kNumberOfAsyncs| number of threads, where one thread closes
// the connection and all other threads perform a reply. Depending on thread
// scheduling, zero or more number of replies may be sent, but all client calls
// must either see a reply or a close and there should not be any thread-related
// data corruptions.
TEST(BindServerTestCase, MultipleAsyncRepliesOnePeerClose) {
  struct AsyncDelayedServer : fidl::WireServer<ValueEcho> {
    AsyncDelayedServer(std::vector<std::unique_ptr<async::Loop>>* loops, sync_completion_t* done)
        : loops_(loops), done_(done) {}
    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      auto worker = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
      // The posted task may run after the server is destroyed. As such, we must
      // not capture server member fields by reference or capture `this`.
      async::PostTask(worker->dispatcher(),
                      [request = std::string(request->s.get()), completer = completer.ToAsync(),
                       done = done_]() mutable {
                        bool signal = false;
                        static std::atomic<int> count;
                        if (++count == kNumberOfAsyncs) {
                          signal = true;
                        }
                        if (signal) {
                          sync_completion_signal(done);
                          completer.Close(ZX_OK);
                        } else {
                          sync_completion_wait(done, ZX_TIME_INFINITE);
                          completer.Reply(fidl::StringView::FromExternal(request));
                        }
                      });
      ASSERT_OK(worker->StartThread());
      loops_->push_back(std::move(worker));
    }
    std::vector<std::unique_ptr<async::Loop>>* loops_;
    sync_completion_t* done_;
  };

  // These state must outlive the server, which is destroyed on peer close.
  sync_completion_t done;
  std::vector<std::unique_ptr<async::Loop>> loops;

  // Server launches a thread so we can make sync client calls.
  auto server = std::make_unique<AsyncDelayedServer>(&loops, &done);
  async::Loop main(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(main.StartThread());

  auto endpoints = fidl::CreateEndpoints<ValueEcho>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t closed;
  fidl::OnUnboundFn<AsyncDelayedServer> on_unbound =
      [&closed](AsyncDelayedServer*, fidl::UnbindInfo info, fidl::ServerEnd<ValueEcho> server_end) {
        EXPECT_EQ(fidl::Reason::kClose, info.reason());
        EXPECT_OK(info.status());
        EXPECT_TRUE(server_end);
        sync_completion_signal(&closed);
      };
  fidl::BindServer(main.dispatcher(), std::move(remote), server.get(), std::move(on_unbound));

  // Sync client calls.
  std::vector<std::unique_ptr<async::Loop>> clients;
  for (uint32_t i = 0; i < kNumberOfAsyncs; ++i) {
    auto client = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    async::PostTask(client->dispatcher(), [local = local.borrow(), client = client.get()]() {
      auto result = fidl::WireCall(local)->Echo(kExpectedReply);
      if (result.status() != ZX_OK && result.status() != ZX_ERR_PEER_CLOSED) {
        FAIL();
      }
      client->Quit();
    });
    ASSERT_OK(client->StartThread());
    clients.push_back(std::move(client));
  }
  for (auto& i : clients) {
    i->JoinThreads();
  }
  ASSERT_OK(sync_completion_wait(&closed, ZX_TIME_INFINITE));

  // Verify the epitaph from Close().
  fidl_epitaph_t epitaph;
  ASSERT_OK(
      local.channel().read(0, &epitaph, nullptr, sizeof(fidl_epitaph_t), 0, nullptr, nullptr));
  EXPECT_EQ(ZX_OK, epitaph.error);
}

TEST(BindServerTestCase, CallbackDestroyOnClientClose) {
  using ::test_empty_protocol::Empty;
  class Server : public fidl::WireServer<Empty> {};
  sync_completion_t unbound;
  auto server = std::make_unique<Server>();
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  auto endpoints = fidl::CreateEndpoints<Empty>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  fidl::OnUnboundFn<Server> on_unbound = [&unbound](Server* server, fidl::UnbindInfo info,
                                                    fidl::ServerEnd<Empty> server_end) {
    EXPECT_EQ(fidl::Reason::kPeerClosed, info.reason());
    EXPECT_EQ(ZX_ERR_PEER_CLOSED, info.status());
    EXPECT_TRUE(server_end);
    sync_completion_signal(&unbound);
  };

  fidl::BindServer(loop.dispatcher(), std::move(remote), std::move(server), std::move(on_unbound));
  loop.RunUntilIdle();
  ASSERT_FALSE(sync_completion_signaled(&unbound));

  local.reset();
  loop.RunUntilIdle();
  ASSERT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(BindServerTestCase, CallbackErrorClientTriggered) {
  struct ErrorServer : fidl::WireServer<ValueEcho> {
    explicit ErrorServer(sync_completion_t* worker_start, sync_completion_t* worker_done)
        : worker_start_(worker_start), worker_done_(worker_done) {}
    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      // Launches a thread so we can hold the transaction in progress.
      worker_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
      async::PostTask(worker_->dispatcher(), [request = std::string(request->s.get()),
                                              completer = completer.ToAsync(), this]() mutable {
        sync_completion_signal(worker_start_);
        sync_completion_wait(worker_done_, ZX_TIME_INFINITE);
        completer.Reply(fidl::StringView::FromExternal(request));
      });
      ASSERT_OK(worker_->StartThread());
    }
    sync_completion_t* worker_start_;
    sync_completion_t* worker_done_;
    std::unique_ptr<async::Loop> worker_;
  };
  sync_completion_t worker_start, worker_done, error;

  // Launches a thread so we can wait on the server error.
  auto server = std::make_unique<ErrorServer>(&worker_start, &worker_done);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<ValueEcho>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  fidl::OnUnboundFn<ErrorServer> on_unbound = [&error](ErrorServer*, fidl::UnbindInfo info,
                                                       fidl::ServerEnd<ValueEcho> server_end) {
    EXPECT_EQ(fidl::Reason::kPeerClosed, info.reason());
    EXPECT_EQ(ZX_ERR_PEER_CLOSED, info.status());
    EXPECT_TRUE(server_end);
    sync_completion_signal(&error);
  };

  fidl::BindServer<ErrorServer>(loop.dispatcher(), std::move(remote), server.get(),
                                std::move(on_unbound));

  ASSERT_FALSE(sync_completion_signaled(&worker_start));
  ASSERT_FALSE(sync_completion_signaled(&worker_done));
  ASSERT_FALSE(sync_completion_signaled(&error));

  // Client launches a thread so we can hold the transaction in progress.
  auto client = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  async::PostTask(client->dispatcher(), [local = local.borrow(), client = client.get()]() {
    auto result = fidl::WireCall(local)->Echo(kExpectedReply);
    if (result.status() != ZX_ERR_CANCELED) {  // Client closes the channel before server replies.
      FAIL();
    }
  });
  ASSERT_OK(client->StartThread());

  // Wait until worker_start so we have an in-flight transaction.
  ASSERT_OK(sync_completion_wait(&worker_start, ZX_TIME_INFINITE));

  // Client closes the channel, triggers an error and on_unbound is called.
  local.reset();

  // Wait for the error callback to be called.
  ASSERT_OK(sync_completion_wait(&error, ZX_TIME_INFINITE));

  // Trigger finishing the only outstanding transaction.
  sync_completion_signal(&worker_done);
  loop.Quit();
}

TEST(BindServerTestCase, DestroyBindingWithPendingCancel) {
  struct WorkingServer : fidl::WireServer<ValueEcho> {
    explicit WorkingServer(sync_completion_t* worker_start, sync_completion_t* worker_done)
        : worker_start_(worker_start), worker_done_(worker_done) {}
    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      sync_completion_signal(worker_start_);
      sync_completion_wait(worker_done_, ZX_TIME_INFINITE);
      completer.Reply(request->s);
      EXPECT_EQ(ZX_ERR_PEER_CLOSED, completer.result_of_reply().status());
    }
    sync_completion_t* worker_start_;
    sync_completion_t* worker_done_;
  };
  sync_completion_t worker_start, worker_done;

  // Launches a new thread for the server so we can wait on the worker.
  auto server = std::make_unique<WorkingServer>(&worker_start, &worker_done);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<ValueEcho>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t closed;
  fidl::OnUnboundFn<WorkingServer> on_unbound = [&closed](WorkingServer*, fidl::UnbindInfo info,
                                                          fidl::ServerEnd<ValueEcho> server_end) {
    EXPECT_EQ(fidl::Reason::kPeerClosed, info.reason());
    EXPECT_EQ(ZX_ERR_PEER_CLOSED, info.status());
    EXPECT_TRUE(server_end);
    sync_completion_signal(&closed);
  };
  fidl::BindServer(loop.dispatcher(), std::move(remote), server.get(), std::move(on_unbound));

  ASSERT_FALSE(sync_completion_signaled(&worker_start));
  ASSERT_FALSE(sync_completion_signaled(&worker_done));
  ASSERT_FALSE(sync_completion_signaled(&closed));

  // Client launches a thread so we can hold the transaction in progress.
  auto client = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  async::PostTask(client->dispatcher(), [local = local.borrow(), client = client.get()]() {
    auto result = fidl::WireCall(local)->Echo(kExpectedReply);
    if (result.status() != ZX_ERR_CANCELED) {  // Client closes the channel before server replies.
      FAIL();
    }
  });
  ASSERT_OK(client->StartThread());

  // Wait until worker_start so we have an in-flight transaction.
  ASSERT_OK(sync_completion_wait(&worker_start, ZX_TIME_INFINITE));

  // Client closes its end of the channel, we trigger an error but can't close until the in-flight
  // transaction is destroyed.
  local.reset();

  // Trigger finishing the transaction, Reply() will fail (closed channel) and the transaction will
  // Close(). We make sure the channel error by the client happens first and the in-flight
  // transaction tries to Reply() second.
  sync_completion_signal(&worker_done);

  // Wait for the closed callback to be called.
  ASSERT_OK(sync_completion_wait(&closed, ZX_TIME_INFINITE));
}

TEST(BindServerTestCase, CallbackErrorServerTriggered) {
  struct ErrorServer : fidl::WireServer<ValueEcho> {
    explicit ErrorServer(sync_completion_t* worker_start, sync_completion_t* worker_done)
        : worker_start_(worker_start), worker_done_(worker_done) {}

    // After the first request, subsequent requests close the channel.
    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      count++;
      if (count > 1) {
        completer.Close(ZX_ERR_INTERNAL);
        return;
      }

      // Launches a thread so we can hold the transaction in progress.
      worker_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
      async::PostTask(worker_->dispatcher(), [request = std::string(request->s.get()),
                                              completer = completer.ToAsync(), this]() mutable {
        sync_completion_signal(worker_start_);
        sync_completion_wait(worker_done_, ZX_TIME_INFINITE);
        completer.Reply(fidl::StringView::FromExternal(request));
      });
      ASSERT_OK(worker_->StartThread());
    }

    sync_completion_t* worker_start_;
    sync_completion_t* worker_done_;
    std::unique_ptr<async::Loop> worker_;
    int count = 0;
  };
  sync_completion_t worker_start, worker_done, closed;

  // Launches a thread so we can wait on the server error.
  auto server = std::make_unique<ErrorServer>(&worker_start, &worker_done);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<ValueEcho>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  fidl::OnUnboundFn<ErrorServer> on_unbound = [&closed](ErrorServer*, fidl::UnbindInfo info,
                                                        fidl::ServerEnd<ValueEcho> server_end) {
    EXPECT_EQ(fidl::Reason::kClose, info.reason());
    EXPECT_OK(info.status());
    EXPECT_TRUE(server_end);
    sync_completion_signal(&closed);
  };

  fidl::BindServer<ErrorServer>(loop.dispatcher(), std::move(remote), server.get(),
                                std::move(on_unbound));

  ASSERT_FALSE(sync_completion_signaled(&worker_start));
  ASSERT_FALSE(sync_completion_signaled(&worker_done));
  ASSERT_FALSE(sync_completion_signaled(&closed));

  // Client1 launches a thread so we can hold its transaction in progress.
  auto client1 = std::thread([local = local.borrow()] {
    // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
    (void)fidl::WireCall(local)->Echo(kExpectedReply);
  });

  // Wait until worker_start so we have an in-flight transaction.
  ASSERT_OK(sync_completion_wait(&worker_start, ZX_TIME_INFINITE));

  // Client2 launches a thread to continue the test while its transaction is still in progress.
  auto client2 = std::thread([local = local.borrow(), &worker_done] {
    // After |worker_start|, this will be the second request the server sees.
    // Server will close the channel.
    auto result = fidl::WireCall(local)->Echo(kExpectedReply);
    if (result.status() != ZX_ERR_PEER_CLOSED) {
      FAIL();
    }
    // Trigger finishing the client1 outstanding transaction.
    sync_completion_signal(&worker_done);
  });

  // Wait for the closed callback to be called.
  ASSERT_OK(sync_completion_wait(&closed, ZX_TIME_INFINITE));

  // Verify the epitaph.
  fidl_epitaph_t epitaph;
  ASSERT_OK(
      local.channel().read(0, &epitaph, nullptr, sizeof(fidl_epitaph_t), 0, nullptr, nullptr));
  EXPECT_EQ(ZX_ERR_INTERNAL, epitaph.error);

  client1.join();
  client2.join();
}

TEST(BindServerTestCase, CallbackDestroyOnServerClose) {
  class Server : public fidl::WireServer<Closer> {
   public:
    explicit Server(sync_completion_t* destroyed) : destroyed_(destroyed) {}
    ~Server() override { sync_completion_signal(destroyed_); }

    void Close(CloseCompleter::Sync& completer) override { completer.Close(ZX_OK); }

   private:
    sync_completion_t* destroyed_;
  };

  sync_completion_t destroyed;
  // Server launches a thread so we can make sync client calls.
  auto server = std::make_unique<Server>(&destroyed);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<Closer>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  fidl::OnUnboundFn<Server> on_unbound = [](Server* server, fidl::UnbindInfo info,
                                            fidl::ServerEnd<Closer> server_end) {
    EXPECT_EQ(fidl::Reason::kClose, info.reason());
    EXPECT_OK(info.status());
    EXPECT_TRUE(server_end);
    delete server;
  };

  fidl::BindServer(loop.dispatcher(), std::move(remote), server.release(), std::move(on_unbound));
  ASSERT_FALSE(sync_completion_signaled(&destroyed));

  auto result = fidl::WireCall(local)->Close();
  EXPECT_EQ(result.status(), ZX_ERR_PEER_CLOSED);

  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));
  // Make sure the other end closed
  ASSERT_OK(local.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr));

  // Verify the epitaph from Close().
  fidl_epitaph_t epitaph;
  ASSERT_OK(
      local.channel().read(0, &epitaph, nullptr, sizeof(fidl_epitaph_t), 0, nullptr, nullptr));
  EXPECT_EQ(ZX_OK, epitaph.error);
}

TEST(BindServerTestCase, ExplicitUnbind) {
  using ::test_empty_protocol::Empty;
  class Server : public fidl::WireServer<Empty> {};
  // Server launches a thread so we can make sync client calls.
  sync_completion_t unbound;
  Server server;
  async::Loop main(&kAsyncLoopConfigNoAttachToCurrentThread);
  main.StartThread();

  auto endpoints = fidl::CreateEndpoints<Empty>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);
  auto remote_handle = remote.channel().get();

  fidl::OnUnboundFn<Server> on_unbound = [&, remote_handle](Server* server, fidl::UnbindInfo info,
                                                            fidl::ServerEnd<Empty> server_end) {
    EXPECT_EQ(fidl::Reason::kUnbind, info.reason());
    EXPECT_OK(info.status());
    EXPECT_EQ(server_end.channel().get(), remote_handle);
    sync_completion_signal(&unbound);
  };
  auto binding_ref =
      fidl::BindServer(main.dispatcher(), std::move(remote), &server, std::move(on_unbound));

  // Unbind() and wait for the hook.
  binding_ref.Unbind();
  ASSERT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(BindServerTestCase, ExplicitUnbindWithPendingTransaction) {
  struct WorkingServer : fidl::WireServer<ValueEcho> {
    explicit WorkingServer(sync_completion_t* worker_start, sync_completion_t* worker_done)
        : worker_start_(worker_start), worker_done_(worker_done) {}
    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      sync_completion_signal(worker_start_);
      sync_completion_wait(worker_done_, ZX_TIME_INFINITE);
      completer.Reply(request->s);
    }
    sync_completion_t* worker_start_;
    sync_completion_t* worker_done_;
  };
  sync_completion_t worker_start, worker_done;

  // Launches a new thread for the server so we can wait on the worker.
  auto server = std::make_unique<WorkingServer>(&worker_start, &worker_done);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<ValueEcho>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);
  zx_handle_t remote_handle = remote.channel().get();

  // Client launches a thread so we can hold the transaction in progress.
  auto client = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  async::PostTask(client->dispatcher(), [local = local.borrow(), client = client.get()]() {
    // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
    (void)fidl::WireCall(local)->Echo(kExpectedReply);
  });
  ASSERT_OK(client->StartThread());

  sync_completion_t unbound;
  fidl::OnUnboundFn<WorkingServer> on_unbound = [remote_handle, &unbound](
                                                    WorkingServer*, fidl::UnbindInfo info,
                                                    fidl::ServerEnd<ValueEcho> server_end) {
    EXPECT_EQ(fidl::Reason::kUnbind, info.reason());
    EXPECT_OK(info.status());
    EXPECT_EQ(server_end.channel().get(), remote_handle);
    sync_completion_signal(&unbound);
  };
  auto binding_ref =
      fidl::BindServer(loop.dispatcher(), std::move(remote), server.get(), std::move(on_unbound));

  // Wait until worker_start so we have an in-flight transaction.
  ASSERT_OK(sync_completion_wait(&worker_start, ZX_TIME_INFINITE));

  // Unbind the server end of the channel.
  binding_ref.Unbind();

  // The unbound hook will not run until the thread inside Echo() returns.
  sync_completion_signal(&worker_done);

  // Wait for the unbound hook.
  ASSERT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

// Checks that sending an event may be performed concurrently from different
// threads while unbinding is occurring, and that those event sending operations
// return |ZX_ERR_CANCELED| after the server has been unbound.
TEST(BindServerTestCase, ConcurrentSendEventWhileUnbinding) {
  class Server : public fidl::WireServer<Values> {
   public:
    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      ADD_FAILURE("Not used in this test");
    }

    void OneWay(OneWayRequestView request, OneWayCompleter::Sync& completer) override {
      ADD_FAILURE("Not used in this test");
    }
  };

  // Repeat the test until at least one failure is observed.
  for (;;) {
    auto endpoints = fidl::CreateEndpoints<Values>();
    ASSERT_OK(endpoints.status_value());
    auto [local, remote] = std::move(*endpoints);

    Server server;

    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    ASSERT_OK(loop.StartThread());

    auto server_binding = fidl::BindServer(loop.dispatcher(), std::move(remote), &server);

    // Start sending events from multiple threads.
    constexpr size_t kNumEventsPerThread = 170;
    constexpr size_t kNumThreads = 10;
    std::atomic<size_t> num_failures = 0;

    std::array<std::thread, kNumThreads> sender_threads;
    sync_completion_t worker_start;
    sync_completion_t worker_running;
    for (size_t i = 0; i < kNumThreads; ++i) {
      sender_threads[i] =
          std::thread([&worker_start, &worker_running, &server_binding, &num_failures]() {
            ZX_ASSERT(ZX_OK == sync_completion_wait(&worker_start, ZX_TIME_INFINITE));
            for (size_t i = 0; i < kNumEventsPerThread; i++) {
              fidl::Status result =
                  fidl::WireSendEvent(server_binding)->OnValueEvent(fidl::StringView("a"));
              if (!result.ok()) {
                // |ZX_ERR_CANCELED| indicates unbinding has happened.
                ZX_ASSERT_MSG(result.status() == ZX_ERR_CANCELED, "Unexpected status: %d",
                              result.status());
                num_failures.fetch_add(1);
              }
              if (i == 0) {
                sync_completion_signal(&worker_running);
              }
            }
          });
    }

    sync_completion_signal(&worker_start);
    ASSERT_OK(sync_completion_wait(&worker_running, ZX_TIME_INFINITE));

    // Unbinds the server before all the threads have been able to send all
    // their events.
    server_binding.Unbind();

    for (auto& t : sender_threads) {
      t.join();
    }

    // The total number of events and failures must add up to the right amount.
    size_t num_success = 0;
    {
      uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
      // Consumes (reads) all the events sent by all the server threads without
      // decoding them.
      while (ZX_OK == local.channel().read(0, bytes, nullptr, sizeof(bytes), 0, nullptr, nullptr)) {
        num_success++;
      }
    }

    ASSERT_GT(num_success, 0);
    ASSERT_EQ(num_success + num_failures, kNumEventsPerThread * kNumThreads);

    // Retry the test if there were no failures due to |Unbind| happening
    // too late.
    if (num_failures.load() > 0) {
      break;
    }
  }
}

TEST(BindServerTestCase, ConcurrentSyncReply) {
  struct ConcurrentSyncServer : fidl::WireServer<ValueEcho> {
    ConcurrentSyncServer(int max_reqs) : max_reqs_(max_reqs) {}
    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      // Increment the request count. Yield to allow other threads to execute.
      auto i = ++req_cnt_;
      zx_thread_legacy_yield(0);
      // Ensure that no other threads have entered Echo() after this thread.
      ASSERT_EQ(i, req_cnt_);
      // Let other threads in.
      completer.EnableNextDispatch();
      // The following should be a NOP. An additional wait should not be added. If it is, the above
      // assertion may fail if two requests arrive concurrently.
      completer.EnableNextDispatch();
      // Calls to Echo() block until max_reqs requests have arrived.
      if (i < max_reqs_) {
        sync_completion_wait(&on_max_reqs_, ZX_TIME_INFINITE);
      } else {
        sync_completion_signal(&on_max_reqs_);
      }
      completer.Reply(request->s);
    }
    sync_completion_t on_max_reqs_;
    const int max_reqs_;
    std::atomic<int> req_cnt_ = 0;
  };

  auto endpoints = fidl::CreateEndpoints<ValueEcho>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  // Launch server with 10 threads.
  constexpr int kMaxReqs = 10;
  auto server = std::make_unique<ConcurrentSyncServer>(kMaxReqs);
  async::Loop server_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  for (int i = 0; i < kMaxReqs; ++i)
    ASSERT_OK(server_loop.StartThread());

  // Bind the server.
  auto res = fidl::BindServer(server_loop.dispatcher(), std::move(remote), server.get());
  fidl::ServerBindingRef<ValueEcho> binding(std::move(res));

  // Launch 10 client threads to make two-way Echo() calls.
  std::vector<std::thread> threads;
  for (int i = 0; i < kMaxReqs; ++i) {
    threads.emplace_back([local = local.borrow()] {
      auto result = fidl::WireCall(local)->Echo(kExpectedReply);
      EXPECT_EQ(result.status(), ZX_OK);
    });
  }

  // Join the client threads.
  for (auto& thread : threads)
    thread.join();

  // Unbind the server.
  binding.Unbind();
}

TEST(BindServerTestCase, ConcurrentIdempotentClose) {
  struct ConcurrentSyncServer : fidl::WireServer<Closer> {
    void Close(CloseCompleter::Sync& completer) override {
      // Add the wait back to the dispatcher. Sleep to allow another thread in.
      completer.EnableNextDispatch();
      zx_thread_legacy_yield(0);
      // Close with ZX_OK.
      completer.Close(ZX_OK);
    }
  };

  auto endpoints = fidl::CreateEndpoints<Closer>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  // Launch server with 10 threads.
  constexpr int kMaxReqs = 10;
  auto server = std::make_unique<ConcurrentSyncServer>();
  async::Loop server_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  for (int i = 0; i < kMaxReqs; ++i)
    ASSERT_OK(server_loop.StartThread());

  // Bind the server.
  sync_completion_t unbound;
  fidl::OnUnboundFn<ConcurrentSyncServer> on_unbound =
      [&unbound](ConcurrentSyncServer*, fidl::UnbindInfo info, fidl::ServerEnd<Closer> server_end) {
        static std::atomic_flag invoked = ATOMIC_FLAG_INIT;
        ASSERT_FALSE(invoked.test_and_set());  // Must only be called once.
        EXPECT_EQ(fidl::Reason::kClose, info.reason());
        EXPECT_OK(info.status());
        EXPECT_TRUE(server_end);
        sync_completion_signal(&unbound);
      };
  fidl::BindServer(server_loop.dispatcher(), std::move(remote), server.get(),
                   std::move(on_unbound));

  // Launch 10 client threads to make two-way Close() calls.
  std::vector<std::thread> threads;
  for (int i = 0; i < kMaxReqs; ++i) {
    threads.emplace_back([local = local.borrow()] {
      auto result = fidl::WireCall(local)->Close();
      EXPECT_EQ(result.status(), ZX_ERR_PEER_CLOSED);
    });
  }

  // Join the client threads.
  for (auto& thread : threads)
    thread.join();

  // Wait for the unbound handler before letting the loop be destroyed.
  ASSERT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

// Tests that the user may ignore sync method completers after |Unbind| returns.
//
// This is useful for synchronously tearing down a server from a sequential
// context, such as unbinding and destroying the server from a single-threaded
// async dispatcher thread.
TEST(BindServerTestCase, UnbindSynchronouslyPassivatesSyncCompleter) {
  // This server destroys itself upon the |Echo| call.
  class ShutdownOnEchoRequestServer : public fidl::WireServer<ValueEcho> {
   public:
    ShutdownOnEchoRequestServer(async::Loop* loop, fidl::ServerEnd<ValueEcho> server_end)
        : binding_ref_(
              fidl::BindServer(loop->dispatcher(), std::move(server_end), this,
                               cpp20::bind_front(&ShutdownOnEchoRequestServer::OnUnbound, loop))) {}

    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      // |Unbind| requests to unbind the server. |completer| is passivated.
      // We will be asynchronously notified of unbind completion via |fidl::OnUnboundFn|.
      binding_ref_.Unbind();
    }

    static void OnUnbound(async::Loop* loop, ShutdownOnEchoRequestServer* server,
                          fidl::UnbindInfo info, fidl::ServerEnd<ValueEcho> server_end) {
      EXPECT_EQ(fidl::Reason::kUnbind, info.reason());
      EXPECT_OK(info.status());
      loop->Quit();
      delete server;
    }

   private:
    fidl::ServerBindingRef<ValueEcho> binding_ref_;
  };

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  zx::result endpoints = fidl::CreateEndpoints<ValueEcho>();
  ASSERT_OK(endpoints.status_value());

  // Server owns itself.
  (void)new ShutdownOnEchoRequestServer(&loop, std::move(endpoints->server));

  std::thread call_thread([client_end = std::move(endpoints->client)] {
    fidl::WireResult result = fidl::WireCall(client_end)->Echo("");
    ASSERT_STATUS(ZX_ERR_PEER_CLOSED, result.status());
  });

  // Loop is shutdown in |OnUnbound|.
  ASSERT_STATUS(ZX_ERR_CANCELED, loop.Run());
  call_thread.join();
}

// Tests that the user may immediately discard pending async method completers
// after |Unbind| returns.
//
// This is useful for synchronously tearing down a server from a sequential
// context, such as unbinding and destroying the server from a single-threaded
// async dispatcher thread.
TEST(BindServerTestCase, UnbindSynchronouslyPassivatesAsyncCompleter) {
  // This server destroys itself upon the |Echo| call.
  class ShutdownOnEchoRequestServer : public fidl::WireServer<ValueEcho> {
   public:
    ShutdownOnEchoRequestServer(async::Loop* loop, fidl::ServerEnd<ValueEcho> server_end)
        : loop_(loop),
          binding_ref_(
              fidl::BindServer(loop->dispatcher(), std::move(server_end), this,
                               cpp20::bind_front(&ShutdownOnEchoRequestServer::OnUnbound, loop))) {}

    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      async_completer_.emplace(completer.ToAsync());

      // Order of events:
      // 1. |Unbind| requests to unbind the server. Completers are passivated.
      // 2. Server and completer are destroyed. This is safe to do from the
      //    single dispatcher thread.
      // 3. We are notified of unbind completion via |fidl::OnUnboundFn|.
      async::PostTask(loop_->dispatcher(), [&] {
        binding_ref_.Unbind();
        delete this;
      });
    }

    static void OnUnbound(async::Loop* loop, ShutdownOnEchoRequestServer* server,
                          fidl::UnbindInfo info, fidl::ServerEnd<ValueEcho> server_end) {
      EXPECT_EQ(fidl::Reason::kUnbind, info.reason());
      EXPECT_OK(info.status());
      loop->Quit();
    }

   private:
    async::Loop* loop_;
    fidl::ServerBindingRef<ValueEcho> binding_ref_;
    std::optional<EchoCompleter::Async> async_completer_;
  };

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  zx::result endpoints = fidl::CreateEndpoints<ValueEcho>();
  ASSERT_OK(endpoints.status_value());

  // Server owns itself.
  (void)new ShutdownOnEchoRequestServer(&loop, std::move(endpoints->server));

  std::thread call_thread([client_end = std::move(endpoints->client)] {
    fidl::WireResult result = fidl::WireCall(client_end)->Echo("");
    ASSERT_STATUS(ZX_ERR_PEER_CLOSED, result.status());
  });

  // Loop is shutdown in |OnUnbound|.
  ASSERT_STATUS(ZX_ERR_CANCELED, loop.Run());
  call_thread.join();
}

// Tests the following corner case:
// - A server method handler is expecting to execute long-running work.
// - Hence it calls |EnableNextDispatch| to allow another dispatcher thread
//   to dispatch the next message while the current handler is still running.
// - Something goes wrong in the next message leading to binding teardown.
// - Teardown should not complete until the initial method handler returns.
//   This is important to avoid use-after-free if the user destroys the server
//   at the point of teardown completion.
TEST(BindServerTestCase, EnableNextDispatchInLongRunningHandler) {
  struct LongOperationServer : fidl::WireServer<Closer> {
    explicit LongOperationServer(libsync::Completion* long_operation)
        : long_operation_(long_operation) {}
    void Close(CloseCompleter::Sync& completer) override {
      if (!first_request_.test_and_set()) {
        completer.EnableNextDispatch();
        long_operation_->Wait();
        completer.Close(ZX_OK);
      } else {
        completer.Close(ZX_OK);
      }
    }

   private:
    std::atomic_flag first_request_ = ATOMIC_FLAG_INIT;
    libsync::Completion* long_operation_;
  };

  zx::result endpoints = fidl::CreateEndpoints<Closer>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  // Launch server with 2 threads.
  libsync::Completion long_operation;
  auto server = std::make_unique<LongOperationServer>(&long_operation);
  async::Loop server_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(server_loop.StartThread());
  ASSERT_OK(server_loop.StartThread());

  libsync::Completion unbound;
  fidl::BindServer(server_loop.dispatcher(), std::move(remote), server.get(),
                   [&unbound](LongOperationServer*, fidl::UnbindInfo, fidl::ServerEnd<Closer>) {
                     unbound.Signal();
                   });

  // Issue two requests. The second request should initiate binding teardown.
  std::vector<std::thread> threads;
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  threads.emplace_back([local = local.borrow()] { (void)fidl::WireCall(local)->Close(); });
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  threads.emplace_back([local = local.borrow()] { (void)fidl::WireCall(local)->Close(); });

  // Teardown should not complete unless |long_operation| completes.
  ASSERT_STATUS(ZX_ERR_TIMED_OUT, unbound.Wait(zx::msec(100)));
  long_operation.Signal();
  ASSERT_OK(unbound.Wait());

  for (auto& thread : threads)
    thread.join();
}

TEST(BindServerTestCase, ServerUnbind) {
  // Create the server.
  class Server : public fidl::WireServer<test_empty_protocol::Empty> {};
  Server server;
  sync_completion_t unbound;
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  // Create and bind the channel.
  auto endpoints = fidl::CreateEndpoints<test_empty_protocol::Empty>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);
  auto remote_handle = remote.channel().get();
  fidl::OnUnboundFn<Server> on_unbound =
      [remote_handle, remote = &remote, &unbound](
          Server* server, fidl::UnbindInfo info,
          fidl::ServerEnd<test_empty_protocol::Empty> server_end) {
        EXPECT_EQ(fidl::Reason::kUnbind, info.reason());
        EXPECT_OK(info.status());
        EXPECT_EQ(server_end.channel().get(), remote_handle);
        *remote = std::move(server_end);
        sync_completion_signal(&unbound);
      };
  auto binding_ref =
      fidl::BindServer(loop.dispatcher(), std::move(remote), &server, std::move(on_unbound));

  // The binding should be destroyed without waiting for the Server to be destroyed.
  binding_ref.Unbind();
  ASSERT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));

  // Unbind()/Close() may still be called from the Server.
  binding_ref.Unbind();
  binding_ref.Close(ZX_OK);

  // The channel should still be valid.
  EXPECT_EQ(remote.channel().get(), remote_handle);

  // No epitaph should have been sent.
  EXPECT_EQ(ZX_ERR_TIMED_OUT,
            local.channel().wait_one(ZX_CHANNEL_READABLE, zx::time::infinite_past(), nullptr));
}

TEST(BindServerTestCase, ServerClose) {
  // Create the server.
  class Server : public fidl::WireServer<test_empty_protocol::Empty> {};
  Server server;
  sync_completion_t unbound;
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  // Create and bind the channel.
  auto endpoints = fidl::CreateEndpoints<test_empty_protocol::Empty>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);
  fidl::OnUnboundFn<Server> on_unbound =
      [&unbound](Server* server, fidl::UnbindInfo info,
                 fidl::ServerEnd<test_empty_protocol::Empty> server_end) {
        EXPECT_EQ(fidl::Reason::kClose, info.reason());
        EXPECT_OK(info.status());
        EXPECT_TRUE(server_end);
        sync_completion_signal(&unbound);
      };
  auto binding_ref =
      fidl::BindServer(loop.dispatcher(), std::move(remote), &server, std::move(on_unbound));

  // The binding should be destroyed without waiting for the Server to be destroyed.
  binding_ref.Close(ZX_OK);
  ASSERT_OK(local.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr));
  ASSERT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));

  // Unbind()/Close() may still be called from the Server.
  binding_ref.Unbind();
  binding_ref.Close(ZX_OK);

  // Verify the epitaph from Close().
  fidl_epitaph_t epitaph;
  ASSERT_OK(
      local.channel().read(0, &epitaph, nullptr, sizeof(fidl_epitaph_t), 0, nullptr, nullptr));
  EXPECT_EQ(ZX_OK, epitaph.error);
}

fidl::Endpoints<Values> CreateEndpointsWithoutServerWriteRight() {
  zx::result endpoints = fidl::CreateEndpoints<Values>();
  EXPECT_OK(endpoints.status_value());
  if (!endpoints.is_ok())
    return {};

  auto [client_end, server_end] = std::move(*endpoints);
  {
    zx::channel server_channel_non_writable;
    EXPECT_OK(
        server_end.channel().replace(ZX_RIGHT_READ | ZX_RIGHT_WAIT, &server_channel_non_writable));
    server_end.channel() = std::move(server_channel_non_writable);
  }

  return fidl::Endpoints<Values>{std::move(client_end), std::move(server_end)};
}

// A mock server that panics upon receiving any message.
class NotImplementedServer : public fidl::testing::WireTestBase<test_basic_protocol::Values> {
  void NotImplemented_(const std::string& name, ::fidl::CompleterBase& completer) final {
    ZX_PANIC("Unreachable");
  }
};

template <typename Protocol>
class UnbindObserver {
 public:
  UnbindObserver(fidl::Reason expected_reason, zx_status_t expected_status,
                 std::string expected_message_substring = "")
      : expected_reason_(expected_reason), expected_status_(expected_status) {
    if (!expected_message_substring.empty()) {
      expected_message_substring_.emplace(std::move(expected_message_substring));
    }
  }

  fidl::OnUnboundFn<fidl::WireServer<Protocol>> GetCallback() {
    fidl::OnUnboundFn<fidl::WireServer<Protocol>> on_unbound =
        [this](fidl::WireServer<Protocol>*, fidl::UnbindInfo info, fidl::ServerEnd<Protocol>) {
          EXPECT_EQ(expected_reason_, info.reason());
          EXPECT_EQ(expected_status_, info.status());
          if (expected_message_substring_.has_value()) {
            EXPECT_SUBSTR(info.FormatDescription().c_str(), expected_message_substring_->c_str());
          }
          completion_.Signal();
        };
    return on_unbound;
  }

  libsync::Completion& completion() { return completion_; }

  bool DidUnbind() const { return completion_.signaled(); }

 private:
  fidl::Reason expected_reason_;
  zx_status_t expected_status_;
  std::optional<std::string> expected_message_substring_;
  libsync::Completion completion_;
};

TEST(BindServerTestCase, UnbindInfoDecodeError) {
  auto server = std::make_unique<NotImplementedServer>();
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::result endpoints = fidl::CreateEndpoints<Values>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  // Error message should contain the word "presence", because the presence
  // marker is invalid. Only checking for "presence" allows the error message to
  // evolve slightly without breaking tests.
  UnbindObserver<Values> observer(fidl::Reason::kDecodeError, ZX_ERR_INVALID_ARGS, "presence");
  fidl::BindServer(loop.dispatcher(), std::move(remote), server.get(), observer.GetCallback());

  // Make a call with an intentionally crafted wrong message.
  // To trigger a decode error, here we use a string with an invalid presence marker.
  fidl::internal::TransactionalRequest<Values::Echo> request;
  request.body.s = fidl::StringView::FromExternal(
      reinterpret_cast<const char*>(0x1234123412341234),  // invalid presence marker
      0                                                   // size
  );
  const zx_channel_call_args_t args{
      .wr_bytes = &request,
      .wr_handles = nullptr,
      .rd_bytes = nullptr,
      .rd_handles = nullptr,
      .wr_num_bytes = sizeof(request),
      .wr_num_handles = 0,
      .rd_num_bytes = 0,
      .rd_num_handles = 0,
  };
  EXPECT_STATUS(ZX_ERR_PEER_CLOSED,
                local.channel().call(0, zx::time::infinite(), &args, nullptr, nullptr));

  ASSERT_OK(observer.completion().Wait());
}

TEST(BindServerTestCase, UnbindInfoDispatcherBeginsShutdownDuringMessageHandling) {
  struct WorkingServer : fidl::WireServer<Values> {
    explicit WorkingServer(std::shared_ptr<async::Loop> loop) : loop_(std::move(loop)) {}
    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      completer.Reply(request->s);
      std::thread shutdown([loop = loop_] { loop->Shutdown(); });
      shutdown.detach();
      // Polling until the dispatcher has entered a shutdown state.
      while (true) {
        if (async::PostTask(loop_->dispatcher(), [] {}) == ZX_ERR_BAD_STATE) {
          return;
        }
        zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));
      }
    }
    void OneWay(OneWayRequestView request, OneWayCompleter::Sync& completer) override {
      ADD_FAILURE("Must not call OneWay");
    }

   private:
    std::shared_ptr<async::Loop> loop_;
  };

  // Launches a new thread for the server so we can wait on the worker.
  std::shared_ptr loop = std::make_shared<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop->StartThread());
  auto server = std::make_unique<WorkingServer>(loop);

  zx::result endpoints = fidl::CreateEndpoints<Values>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  UnbindObserver<Values> observer(fidl::Reason::kDispatcherError, ZX_ERR_CANCELED);
  fidl::BindServer(loop->dispatcher(), std::move(remote), server.get(), observer.GetCallback());

  fidl::WireResult result = fidl::WireCall(local)->Echo("");
  EXPECT_OK(result.status());

  ASSERT_OK(observer.completion().Wait());
}

// Error sending reply should trigger binding teardown.
TEST(BindServerTestCase, UnbindInfoErrorSendingReply) {
  struct WorkingServer : fidl::WireServer<Values> {
    WorkingServer() = default;
    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      completer.Reply(request->s);
      EXPECT_EQ(ZX_ERR_ACCESS_DENIED, completer.result_of_reply().status());
    }
    void OneWay(OneWayRequestView request, OneWayCompleter::Sync& completer) override {
      ADD_FAILURE("Must not call OneWay");
    }
  };

  // Launches a new thread for the server so we can wait on the worker.
  auto server = std::make_unique<WorkingServer>();
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  fidl::Endpoints<Values> endpoints;
  ASSERT_NO_FAILURES(endpoints = CreateEndpointsWithoutServerWriteRight());
  auto [local, remote] = std::move(endpoints);

  UnbindObserver<Values> observer(fidl::Reason::kTransportError, ZX_ERR_ACCESS_DENIED);
  fidl::BindServer(loop.dispatcher(), std::move(remote), server.get(), observer.GetCallback());

  fidl::WireResult result = fidl::WireCall(local)->Echo("");
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, result.status());

  ASSERT_OK(observer.completion().Wait());
}

// Error sending events should trigger binding teardown.
TEST(BindServerTestCase, UnbindInfoErrorSendingEvent) {
  auto server = std::make_unique<NotImplementedServer>();
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  fidl::Endpoints<Values> endpoints;
  ASSERT_NO_FAILURES(endpoints = CreateEndpointsWithoutServerWriteRight());
  auto [local, remote] = std::move(endpoints);

  UnbindObserver<Values> observer(fidl::Reason::kTransportError, ZX_ERR_ACCESS_DENIED);
  fidl::ServerBindingRef<Values> binding =
      fidl::BindServer(loop.dispatcher(), std::move(remote), server.get(), observer.GetCallback());

  fidl::Status result = fidl::WireSendEvent(binding)->OnValueEvent("");
  ASSERT_STATUS(ZX_ERR_ACCESS_DENIED, result.status());

  ASSERT_FALSE(observer.DidUnbind());
  ASSERT_OK(loop.RunUntilIdle());
  ASSERT_TRUE(observer.DidUnbind());
}

// If a reply or event fails due to a peer closed error, the server bindings
// should still process any remaining messages received on the endpoint before
// tearing down.
TEST(BindServerTestCase, DrainAllMessageInPeerClosedSendErrorEvent) {
  constexpr static char kData[] = "test";
  struct MockServer : fidl::WireServer<Values> {
    MockServer() = default;
    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      ADD_FAILURE("Must not call Echo");
    }
    void OneWay(OneWayRequestView request, OneWayCompleter::Sync& completer) override {
      EXPECT_EQ(request->in.get(), kData);
      called_ = true;
    }

    bool called() const { return called_; }

   private:
    bool called_ = false;
  };

  auto server = std::make_unique<MockServer>();
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  zx::result endpoints = fidl::CreateEndpoints<Values>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  UnbindObserver<Values> observer(fidl::Reason::kPeerClosed, ZX_ERR_PEER_CLOSED);
  fidl::ServerBindingRef<Values> binding =
      fidl::BindServer(loop.dispatcher(), std::move(remote), server.get(), observer.GetCallback());

  // Make a call and close the client endpoint.
  ASSERT_OK(fidl::WireCall(local)->OneWay(kData).status());
  local.reset();

  // Sending event fails due to client endpoint closing.
  fidl::Status result = fidl::WireSendEvent(binding)->OnValueEvent("");
  ASSERT_STATUS(ZX_ERR_PEER_CLOSED, result.status());

  // The initial call should still be processed.
  ASSERT_FALSE(observer.DidUnbind());
  ASSERT_FALSE(server->called());
  ASSERT_OK(loop.RunUntilIdle());
  ASSERT_TRUE(observer.DidUnbind());
  ASSERT_TRUE(server->called());
}

TEST(BindServerTestCase, DrainAllMessageInPeerClosedSendErrorReply) {
  constexpr static char kData[] = "test";
  struct MockServer : fidl::WireServer<Values> {
    MockServer() = default;
    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      // Sending reply fails due to client endpoint closing.
      EXPECT_EQ(request->s.get(), kData);
      completer.Reply(kData);
      fidl::Status result = completer.result_of_reply();
      EXPECT_STATUS(ZX_ERR_PEER_CLOSED, result.status());
      two_way_called_ = true;
    }
    void OneWay(OneWayRequestView request, OneWayCompleter::Sync& completer) override {
      EXPECT_EQ(request->in.get(), kData);
      one_way_called_ = true;
    }

    bool two_way_called() const { return two_way_called_; }
    bool one_way_called() const { return one_way_called_; }

   private:
    bool two_way_called_ = false;
    bool one_way_called_ = false;
  };

  auto server = std::make_unique<MockServer>();
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  zx::result endpoints = fidl::CreateEndpoints<Values>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  UnbindObserver<Values> observer(fidl::Reason::kPeerClosed, ZX_ERR_PEER_CLOSED);
  fidl::ServerBindingRef<Values> binding =
      fidl::BindServer(loop.dispatcher(), std::move(remote), server.get(), observer.GetCallback());

  // Make a two-way call followed by a one-way call and close the client
  // endpoint without monitoring the reply.
  {
    async::Loop client_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    fidl::WireClient client(std::move(local), client_loop.dispatcher());
    client->Echo(kData).ThenExactlyOnce([](fidl::WireUnownedResult<Values::Echo>&) {});
    ASSERT_OK(client->OneWay(kData).status());
    ASSERT_OK(client_loop.RunUntilIdle());
  }

  // The one-way call should still be processed.
  ASSERT_FALSE(observer.DidUnbind());
  ASSERT_FALSE(server->two_way_called());
  ASSERT_FALSE(server->one_way_called());
  ASSERT_OK(loop.RunUntilIdle());
  ASSERT_TRUE(observer.DidUnbind());
  ASSERT_TRUE(server->two_way_called());
  ASSERT_TRUE(server->one_way_called());
}

TEST(BindServerTestCase, UnbindInfoDispatcherError) {
  // Create the server.
  class Server : public fidl::WireServer<test_empty_protocol::Empty> {};
  Server server;
  sync_completion_t unbound;
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  // Create and bind the channel.
  auto endpoints = fidl::CreateEndpoints<test_empty_protocol::Empty>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);
  auto remote_handle = remote.channel().get();
  fidl::OnUnboundFn<Server> on_unbound =
      [remote_handle, remote = &remote, &unbound](
          Server* server, fidl::UnbindInfo info,
          fidl::ServerEnd<test_empty_protocol::Empty> server_end) {
        EXPECT_EQ(fidl::Reason::kDispatcherError, info.reason());
        EXPECT_EQ(ZX_ERR_CANCELED, info.status());
        EXPECT_EQ(server_end.channel().get(), remote_handle);
        *remote = std::move(server_end);
        sync_completion_signal(&unbound);
      };
  auto binding_ref =
      fidl::BindServer(loop.dispatcher(), std::move(remote), &server, std::move(on_unbound));

  // This should destroy the binding, running the error handler before returning.
  loop.Shutdown();
  ASSERT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE_PAST));

  // The channel should still be valid.
  EXPECT_EQ(remote.channel().get(), remote_handle);

  // No epitaph should have been sent.
  EXPECT_EQ(ZX_ERR_TIMED_OUT,
            local.channel().wait_one(ZX_CHANNEL_READABLE, zx::time::infinite_past(), nullptr));
}

TEST(BindServerTestCase, UnbindInfoUnknownMethod) {
  auto server = std::make_unique<NotImplementedServer>();
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  auto endpoints = fidl::CreateEndpoints<Values>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  UnbindObserver<Values> observer(fidl::Reason::kUnexpectedMessage, ZX_ERR_NOT_SUPPORTED);
  fidl::BindServer(loop.dispatcher(), std::move(remote), std::move(server), observer.GetCallback());
  loop.RunUntilIdle();
  ASSERT_FALSE(observer.DidUnbind());

  // An epitaph is never a valid message to a server.
  fidl_epitaph_write(local.channel().get(), ZX_OK);

  loop.RunUntilIdle();
  ASSERT_TRUE(observer.DidUnbind());
}

TEST(BindServerTestCase, ReplyNotRequiredAfterUnbound) {
  struct WorkingServer : fidl::WireServer<ValueEcho> {
    explicit WorkingServer(std::optional<EchoCompleter::Async>* async_completer,
                           sync_completion_t* ready)
        : async_completer_(async_completer), ready_(ready) {}
    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      sync_completion_signal(ready_);
      *async_completer_ = completer.ToAsync();  // Releases ownership of the binding.
    }
    std::optional<EchoCompleter::Async>* async_completer_;
    sync_completion_t* ready_;
  };

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  // Create the channel and bind it with the server and dispatcher.
  auto endpoints = fidl::CreateEndpoints<ValueEcho>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);
  sync_completion_t ready, unbound;
  std::optional<WorkingServer::EchoCompleter::Async> async_completer;
  auto server = std::make_unique<WorkingServer>(&async_completer, &ready);
  fidl::OnUnboundFn<WorkingServer> on_unbound = [&unbound](WorkingServer*, fidl::UnbindInfo info,
                                                           fidl::ServerEnd<ValueEcho>) {
    EXPECT_EQ(fidl::Reason::kUnbind, info.reason());
    EXPECT_EQ(ZX_OK, info.status());
    sync_completion_signal(&unbound);
  };
  auto binding_ref =
      fidl::BindServer(loop.dispatcher(), std::move(remote), server.get(), std::move(on_unbound));

  // Start another thread to make the outgoing call.
  auto other_call_thread = std::thread([local = std::move(local)]() mutable {
    auto result = fidl::WireCall(local)->Echo(kExpectedReply);
    EXPECT_EQ(ZX_ERR_PEER_CLOSED, result.status());
  });

  // Wait for the server to enter Echo().
  ASSERT_OK(sync_completion_wait(&ready, ZX_TIME_INFINITE));

  // Unbind the server.
  binding_ref.Unbind();

  // Wait for the OnUnboundFn.
  ASSERT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));

  // The AsyncCompleter will be destroyed without having Reply()d or Close()d
  // but should not crash.
  other_call_thread.join();
}

// These classes are used to create a server implementation with multiple
// inheritance.
class PlaceholderBase1 {
 public:
  virtual void Foo() = 0;
  int a;
};

class PlaceholderBase2 {
 public:
  virtual void Bar() = 0;
  int b;
};

class MultiInheritanceServer : public PlaceholderBase1,
                               public fidl::WireServer<Closer>,
                               public PlaceholderBase2 {
 public:
  explicit MultiInheritanceServer(sync_completion_t* destroyed) : destroyed_(destroyed) {}
  MultiInheritanceServer(MultiInheritanceServer&& other) = delete;
  MultiInheritanceServer(const MultiInheritanceServer& other) = delete;
  MultiInheritanceServer& operator=(MultiInheritanceServer&& other) = delete;
  MultiInheritanceServer& operator=(const MultiInheritanceServer& other) = delete;

  ~MultiInheritanceServer() override { sync_completion_signal(destroyed_); }

  void Close(CloseCompleter::Sync& completer) override { completer.Close(ZX_OK); }

  void Foo() override {}
  void Bar() override {}

 private:
  sync_completion_t* destroyed_;
};

TEST(BindServerTestCase, MultipleInheritanceServer) {
  sync_completion_t destroyed;
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  // Launch a thread so we can make a blocking client call
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<Closer>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  fidl::OnUnboundFn<MultiInheritanceServer> on_unbound = [](MultiInheritanceServer* server,
                                                            fidl::UnbindInfo info,
                                                            fidl::ServerEnd<Closer> server_end) {
    EXPECT_EQ(fidl::Reason::kClose, info.reason());
    EXPECT_OK(info.status());
    EXPECT_TRUE(server_end);
    delete server;
  };

  fidl::BindServer(loop.dispatcher(), std::move(remote), new MultiInheritanceServer(&destroyed),
                   std::move(on_unbound));
  ASSERT_FALSE(sync_completion_signaled(&destroyed));

  auto result = fidl::WireCall(local)->Close();
  EXPECT_EQ(result.status(), ZX_ERR_PEER_CLOSED);

  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));
  // Make sure the other end closed
  ASSERT_OK(local.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr));

  // Verify the epitaph from Close().
  fidl_epitaph_t epitaph;
  ASSERT_OK(
      local.channel().read(0, &epitaph, nullptr, sizeof(fidl_epitaph_t), 0, nullptr, nullptr));
  EXPECT_EQ(ZX_OK, epitaph.error);
}

TEST(WireSendEvent, UnownedServerEnd) {
  auto endpoints = fidl::CreateEndpoints<Values>();
  ASSERT_EQ(endpoints.status_value(), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  fidl::UnownedServerEnd<Values> server_end(endpoints->server);
  auto result = fidl::WireSendEvent(server_end)->OnValueEvent("abcd");
  ASSERT_OK(result.status());

  // For simplicity, just ensure that *some* message was received on the other side.
  uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  uint32_t byte_actual, handle_actual;
  ASSERT_OK(endpoints->client.channel().read(0, bytes, nullptr, ZX_CHANNEL_MAX_MSG_BYTES, 0,
                                             &byte_actual, &handle_actual));
}

}  // namespace
