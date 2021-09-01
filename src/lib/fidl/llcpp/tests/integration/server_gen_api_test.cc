// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.test.coding.fuchsia/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/sync/completion.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>

#include <thread>
#include <vector>

#include <zxtest/zxtest.h>

namespace {

using ::fidl_test_coding_fuchsia::Simple;

constexpr uint32_t kNumberOfAsyncs = 10;
constexpr int32_t kExpectedReply = 7;

class Server : public fidl::WireServer<Simple> {
 public:
  explicit Server(sync_completion_t* destroyed) : destroyed_(destroyed) {}
  Server(Server&& other) = delete;
  Server(const Server& other) = delete;
  Server& operator=(Server&& other) = delete;
  Server& operator=(const Server& other) = delete;

  ~Server() override { sync_completion_signal(destroyed_); }

  void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
    completer.Reply(request->request);
  }
  void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
    completer.Close(ZX_OK);
  }

 private:
  sync_completion_t* destroyed_;
};

TEST(BindServerTestCase, SyncReply) {
  struct SyncServer : fidl::WireServer<Simple> {
    void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
      ADD_FAILURE("Must not call close");
    }
    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      EXPECT_TRUE(completer.is_reply_needed());
      completer.Reply(request->request);
      EXPECT_FALSE(completer.is_reply_needed());
    }
  };

  // Server launches a thread so we can make sync client calls.
  auto server = std::make_unique<SyncServer>();
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<Simple>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t closed;
  fidl::OnUnboundFn<SyncServer> on_unbound = [&closed](SyncServer*, fidl::UnbindInfo info,
                                                       fidl::ServerEnd<Simple> server_end) {
    EXPECT_EQ(fidl::Reason::kPeerClosed, info.reason());
    EXPECT_EQ(ZX_ERR_PEER_CLOSED, info.status());
    EXPECT_TRUE(server_end);
    sync_completion_signal(&closed);
  };
  fidl::BindServer(loop.dispatcher(), std::move(remote), server.get(), std::move(on_unbound));

  // Sync client call.
  auto result = WireCall(local).Echo(kExpectedReply);
  EXPECT_OK(result.status());
  EXPECT_EQ(result->reply, kExpectedReply);

  local.reset();  // To trigger binding destruction before loop's destruction.
  ASSERT_OK(sync_completion_wait(&closed, ZX_TIME_INFINITE));
}

TEST(BindServerTestCase, AsyncReply) {
  struct AsyncServer : fidl::WireServer<Simple> {
    void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
      ADD_FAILURE("Must not call close");
    }
    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      worker_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
      async::PostTask(worker_->dispatcher(),
                      [request = request->request, completer = completer.ToAsync()]() mutable {
                        EXPECT_TRUE(completer.is_reply_needed());
                        completer.Reply(request);
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

  auto endpoints = fidl::CreateEndpoints<Simple>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t closed;
  fidl::OnUnboundFn<AsyncServer> on_unbound = [&closed](AsyncServer*, fidl::UnbindInfo info,
                                                        fidl::ServerEnd<Simple> server_end) {
    EXPECT_EQ(fidl::Reason::kPeerClosed, info.reason());
    EXPECT_EQ(ZX_ERR_PEER_CLOSED, info.status());
    EXPECT_TRUE(server_end);
    sync_completion_signal(&closed);
  };
  fidl::BindServer(main.dispatcher(), std::move(remote), server.get(), std::move(on_unbound));

  // Sync client call.
  auto result = WireCall(local).Echo(kExpectedReply);
  EXPECT_OK(result.status());
  EXPECT_EQ(result->reply, kExpectedReply);

  local.reset();  // To trigger binding destruction before main's destruction.
  ASSERT_OK(sync_completion_wait(&closed, ZX_TIME_INFINITE));
}

TEST(BindServerTestCase, MultipleAsyncReplies) {
  struct AsyncDelayedServer : fidl::WireServer<Simple> {
    void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
      ADD_FAILURE("Must not call close");
    }
    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      auto worker = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
      async::PostTask(worker->dispatcher(), [request = request->request,
                                             completer = completer.ToAsync(), this]() mutable {
        static std::atomic<int> count;
        // Since we block until we get kNumberOfAsyncs concurrent requests
        // this can only pass if we allow concurrent async replies.
        if (++count == kNumberOfAsyncs) {
          sync_completion_signal(&done_);
        }
        sync_completion_wait(&done_, ZX_TIME_INFINITE);
        completer.Reply(request);
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

  auto endpoints = fidl::CreateEndpoints<Simple>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t closed;
  fidl::OnUnboundFn<AsyncDelayedServer> on_unbound = [&closed](AsyncDelayedServer* server,
                                                               fidl::UnbindInfo info,
                                                               fidl::ServerEnd<Simple> server_end) {
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
      auto result = WireCall(local).Echo(kExpectedReply);
      ASSERT_EQ(result->reply, kExpectedReply);
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

TEST(BindServerTestCase, MultipleAsyncRepliesOnePeerClose) {
  struct AsyncDelayedServer : fidl::WireServer<Simple> {
    AsyncDelayedServer(std::vector<std::unique_ptr<async::Loop>>* loops) : loops_(loops) {}
    void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
      ADD_FAILURE("Must not call close");
    }
    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      auto worker = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
      async::PostTask(worker->dispatcher(), [request = request->request,
                                             completer = completer.ToAsync(), this]() mutable {
        bool signal = false;
        static std::atomic<int> count;
        if (++count == kNumberOfAsyncs) {
          signal = true;
        }
        if (signal) {
          sync_completion_signal(&done_);
          completer.Close(ZX_OK);  // Peer close.
        } else {
          sync_completion_wait(&done_, ZX_TIME_INFINITE);
          completer.Reply(request);
        }
      });
      ASSERT_OK(worker->StartThread());
      loops_->push_back(std::move(worker));
    }
    sync_completion_t done_;
    std::vector<std::unique_ptr<async::Loop>>* loops_;
  };

  // Loops must outlive the server, which is destroyed on peer close.
  std::vector<std::unique_ptr<async::Loop>> loops;
  // Server launches a thread so we can make sync client calls.
  auto server = std::make_unique<AsyncDelayedServer>(&loops);
  async::Loop main(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(main.StartThread());

  auto endpoints = fidl::CreateEndpoints<Simple>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t closed;
  fidl::OnUnboundFn<AsyncDelayedServer> on_unbound =
      [&closed](AsyncDelayedServer*, fidl::UnbindInfo info, fidl::ServerEnd<Simple> server_end) {
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
      auto result = WireCall(local).Echo(kExpectedReply);
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
  sync_completion_t destroyed;
  auto server = std::make_unique<Server>(&destroyed);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  auto endpoints = fidl::CreateEndpoints<Simple>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  fidl::OnUnboundFn<Server> on_unbound = [](Server* server, fidl::UnbindInfo info,
                                            fidl::ServerEnd<Simple> server_end) {
    EXPECT_EQ(fidl::Reason::kPeerClosed, info.reason());
    EXPECT_EQ(ZX_ERR_PEER_CLOSED, info.status());
    EXPECT_TRUE(server_end);
    delete server;
  };

  fidl::BindServer(loop.dispatcher(), std::move(remote), server.release(), std::move(on_unbound));
  loop.RunUntilIdle();
  ASSERT_FALSE(sync_completion_signaled(&destroyed));

  local.reset();
  loop.RunUntilIdle();
  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));
}

TEST(BindServerTestCase, CallbackErrorClientTriggered) {
  struct ErrorServer : fidl::WireServer<Simple> {
    explicit ErrorServer(sync_completion_t* worker_start, sync_completion_t* worker_done)
        : worker_start_(worker_start), worker_done_(worker_done) {}
    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      // Launches a thread so we can hold the transaction in progress.
      worker_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
      async::PostTask(worker_->dispatcher(), [request = request->request,
                                              completer = completer.ToAsync(), this]() mutable {
        sync_completion_signal(worker_start_);
        sync_completion_wait(worker_done_, ZX_TIME_INFINITE);
        completer.Reply(request);
      });
      ASSERT_OK(worker_->StartThread());
    }
    void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
      ADD_FAILURE("Must not call close");
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

  auto endpoints = fidl::CreateEndpoints<Simple>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  fidl::OnUnboundFn<ErrorServer> on_unbound = [&error](ErrorServer*, fidl::UnbindInfo info,
                                                       fidl::ServerEnd<Simple> server_end) {
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
    auto result = WireCall(local).Echo(kExpectedReply);
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
  struct WorkingServer : fidl::WireServer<Simple> {
    explicit WorkingServer(sync_completion_t* worker_start, sync_completion_t* worker_done)
        : worker_start_(worker_start), worker_done_(worker_done) {}
    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      sync_completion_signal(worker_start_);
      sync_completion_wait(worker_done_, ZX_TIME_INFINITE);
      EXPECT_EQ(ZX_ERR_PEER_CLOSED, completer.Reply(request->request).status());
    }
    void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
      ADD_FAILURE("Must not call close");
    }
    sync_completion_t* worker_start_;
    sync_completion_t* worker_done_;
  };
  sync_completion_t worker_start, worker_done;

  // Launches a new thread for the server so we can wait on the worker.
  auto server = std::make_unique<WorkingServer>(&worker_start, &worker_done);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<Simple>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t closed;
  fidl::OnUnboundFn<WorkingServer> on_unbound = [&closed](WorkingServer*, fidl::UnbindInfo info,
                                                          fidl::ServerEnd<Simple> server_end) {
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
    auto result = WireCall(local).Echo(kExpectedReply);
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
  struct ErrorServer : fidl::WireServer<Simple> {
    explicit ErrorServer(sync_completion_t* worker_start, sync_completion_t* worker_done)
        : worker_start_(worker_start), worker_done_(worker_done) {}
    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      // Launches a thread so we can hold the transaction in progress.
      worker_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
      async::PostTask(worker_->dispatcher(), [request = request->request,
                                              completer = completer.ToAsync(), this]() mutable {
        sync_completion_signal(worker_start_);
        sync_completion_wait(worker_done_, ZX_TIME_INFINITE);
        completer.Reply(request);
      });
      ASSERT_OK(worker_->StartThread());
    }
    void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
      completer.Close(ZX_ERR_INTERNAL);
    }
    sync_completion_t* worker_start_;
    sync_completion_t* worker_done_;
    std::unique_ptr<async::Loop> worker_;
  };
  sync_completion_t worker_start, worker_done, closed;

  // Launches a thread so we can wait on the server error.
  auto server = std::make_unique<ErrorServer>(&worker_start, &worker_done);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<Simple>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  fidl::OnUnboundFn<ErrorServer> on_unbound = [&closed](ErrorServer*, fidl::UnbindInfo info,
                                                        fidl::ServerEnd<Simple> server_end) {
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
  auto client1 = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  async::PostTask(client1->dispatcher(),
                  [local = local.borrow()]() { WireCall(local).Echo(kExpectedReply); });
  ASSERT_OK(client1->StartThread());

  // Wait until worker_start so we have an in-flight transaction.
  ASSERT_OK(sync_completion_wait(&worker_start, ZX_TIME_INFINITE));

  // Client2 launches a thread to continue the test while its transaction is still in progress.
  auto client2 = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  async::PostTask(client2->dispatcher(), [local = local.borrow()]() {
    // Server will close the channel, on_unbound is not called.
    auto result = WireCall(local).Close();
    if (result.status() != ZX_ERR_PEER_CLOSED) {
      FAIL();
    }
  });
  ASSERT_OK(client2->StartThread());

  // Trigger finishing the client1 outstanding transaction.
  sync_completion_signal(&worker_done);

  // Wait for the closed callback to be called.
  ASSERT_OK(sync_completion_wait(&closed, ZX_TIME_INFINITE));

  // Verify the epitaph from Close().
  fidl_epitaph_t epitaph;
  ASSERT_OK(
      local.channel().read(0, &epitaph, nullptr, sizeof(fidl_epitaph_t), 0, nullptr, nullptr));
  EXPECT_EQ(ZX_ERR_INTERNAL, epitaph.error);
}

TEST(BindServerTestCase, CallbackDestroyOnServerClose) {
  sync_completion_t destroyed;
  // Server launches a thread so we can make sync client calls.
  auto server = std::make_unique<Server>(&destroyed);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<Simple>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  fidl::OnUnboundFn<Server> on_unbound = [](Server* server, fidl::UnbindInfo info,
                                            fidl::ServerEnd<Simple> server_end) {
    EXPECT_EQ(fidl::Reason::kClose, info.reason());
    EXPECT_OK(info.status());
    EXPECT_TRUE(server_end);
    delete server;
  };

  fidl::BindServer(loop.dispatcher(), std::move(remote), server.release(), std::move(on_unbound));
  ASSERT_FALSE(sync_completion_signaled(&destroyed));

  auto result = WireCall(local).Close();
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
  // Server launches a thread so we can make sync client calls.
  sync_completion_t destroyed;
  auto server = new Server(&destroyed);
  async::Loop main(&kAsyncLoopConfigNoAttachToCurrentThread);
  main.StartThread();

  auto endpoints = fidl::CreateEndpoints<Simple>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);
  auto remote_handle = remote.channel().get();

  fidl::OnUnboundFn<Server> on_unbound = [remote_handle](Server* server, fidl::UnbindInfo info,
                                                         fidl::ServerEnd<Simple> server_end) {
    EXPECT_EQ(fidl::Reason::kUnbind, info.reason());
    EXPECT_OK(info.status());
    EXPECT_EQ(server_end.channel().get(), remote_handle);
    delete server;
  };
  auto binding_ref =
      fidl::BindServer(main.dispatcher(), std::move(remote), server, std::move(on_unbound));

  // Unbind() and wait for the hook.
  binding_ref.Unbind();
  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));
}

TEST(BindServerTestCase, ExplicitUnbindWithPendingTransaction) {
  struct WorkingServer : fidl::WireServer<Simple> {
    explicit WorkingServer(sync_completion_t* worker_start, sync_completion_t* worker_done)
        : worker_start_(worker_start), worker_done_(worker_done) {}
    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      sync_completion_signal(worker_start_);
      sync_completion_wait(worker_done_, ZX_TIME_INFINITE);
      completer.Reply(request->request);
    }
    void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
      ADD_FAILURE("Must not call close");
    }
    sync_completion_t* worker_start_;
    sync_completion_t* worker_done_;
  };
  sync_completion_t worker_start, worker_done;

  // Launches a new thread for the server so we can wait on the worker.
  auto server = std::make_unique<WorkingServer>(&worker_start, &worker_done);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<Simple>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);
  zx_handle_t remote_handle = remote.channel().get();

  // Client launches a thread so we can hold the transaction in progress.
  auto client = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  async::PostTask(client->dispatcher(), [local = local.borrow(), client = client.get()]() {
    WireCall(local).Echo(kExpectedReply);
  });
  ASSERT_OK(client->StartThread());

  sync_completion_t unbound;
  fidl::OnUnboundFn<WorkingServer> on_unbound = [remote_handle, &unbound](
                                                    WorkingServer*, fidl::UnbindInfo info,
                                                    fidl::ServerEnd<Simple> server_end) {
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
  using ::fidl_test_coding_fuchsia::Example;
  class Server : public fidl::WireServer<Example> {
   public:
    void TwoWay(TwoWayRequestView request, TwoWayCompleter::Sync& completer) override {
      ADD_FAILURE("Not used in this test");
    }

    void OneWay(OneWayRequestView request, OneWayCompleter::Sync& completer) override {
      ADD_FAILURE("Not used in this test");
    }
  };

  // Repeat the test until at least one failure is observed.
  for (;;) {
    auto endpoints = fidl::CreateEndpoints<Example>();
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
              zx_status_t status = server_binding->OnEvent(fidl::StringView("a"));
              if (status != ZX_OK) {
                // |ZX_ERR_CANCELED| indicates unbinding has happened.
                ZX_ASSERT_MSG(status == ZX_ERR_CANCELED, "Unexpected status: %d", status);
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
  struct ConcurrentSyncServer : fidl::WireServer<Simple> {
    ConcurrentSyncServer(int max_reqs) : max_reqs_(max_reqs) {}
    void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
      ADD_FAILURE("Must not call close");
    }
    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      // Increment the request count. Yield to allow other threads to execute.
      auto i = ++req_cnt_;
      zx_nanosleep(0);
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
      completer.Reply(request->request);
    }
    sync_completion_t on_max_reqs_;
    const int max_reqs_;
    std::atomic<int> req_cnt_ = 0;
  };

  auto endpoints = fidl::CreateEndpoints<Simple>();
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
  fidl::ServerBindingRef<Simple> binding(std::move(res));

  // Launch 10 client threads to make two-way Echo() calls.
  std::vector<std::thread> threads;
  for (int i = 0; i < kMaxReqs; ++i) {
    threads.emplace_back([local = local.borrow()] {
      auto result = WireCall(local).Echo(kExpectedReply);
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
  struct ConcurrentSyncServer : fidl::WireServer<Simple> {
    void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
      // Add the wait back to the dispatcher. Sleep to allow another thread in.
      completer.EnableNextDispatch();
      zx_nanosleep(0);
      // Close with ZX_OK.
      completer.Close(ZX_OK);
    }
    void Echo(EchoRequestView, EchoCompleter::Sync&) override { ADD_FAILURE("Must not call echo"); }
  };

  auto endpoints = fidl::CreateEndpoints<Simple>();
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
      [&unbound](ConcurrentSyncServer*, fidl::UnbindInfo info, fidl::ServerEnd<Simple> server_end) {
        static std::atomic_flag invoked = ATOMIC_FLAG_INIT;
        ASSERT_FALSE(invoked.test_and_set());  // Must only be called once.
        EXPECT_EQ(fidl::Reason::kClose, info.reason());
        EXPECT_OK(info.status());
        EXPECT_TRUE(server_end);
        sync_completion_signal(&unbound);
      };
  fidl::BindServer(server_loop.dispatcher(), std::move(remote), server.get(),
                   std::move(on_unbound));

  // Launch 10 client threads to make two-way Echo() calls.
  std::vector<std::thread> threads;
  for (int i = 0; i < kMaxReqs; ++i) {
    threads.emplace_back([local = local.borrow()] {
      auto result = WireCall(local).Close();
      EXPECT_EQ(result.status(), ZX_ERR_PEER_CLOSED);
    });
  }

  // Join the client threads.
  for (auto& thread : threads)
    thread.join();

  // Wait for the unbound handler before letting the loop be destroyed.
  ASSERT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(BindServerTestCase, ServerUnbind) {
  // Create the server.
  sync_completion_t destroyed;
  auto* server = new Server(&destroyed);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  // Create and bind the channel.
  auto endpoints = fidl::CreateEndpoints<Simple>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);
  auto remote_handle = remote.channel().get();
  fidl::OnUnboundFn<Server> on_unbound = [remote_handle, remote = &remote](
                                             Server* server, fidl::UnbindInfo info,
                                             fidl::ServerEnd<Simple> server_end) {
    EXPECT_EQ(fidl::Reason::kUnbind, info.reason());
    EXPECT_OK(info.status());
    EXPECT_EQ(server_end.channel().get(), remote_handle);
    *remote = std::move(server_end);
    delete server;
  };
  auto binding_ref =
      fidl::BindServer(loop.dispatcher(), std::move(remote), server, std::move(on_unbound));

  // The binding should be destroyed without waiting for the Server to be destroyed.
  binding_ref.Unbind();
  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));

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
  sync_completion_t destroyed;
  auto* server = new Server(&destroyed);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  // Create and bind the channel.
  auto endpoints = fidl::CreateEndpoints<Simple>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);
  fidl::OnUnboundFn<Server> on_unbound = [](Server* server, fidl::UnbindInfo info,
                                            fidl::ServerEnd<Simple> server_end) {
    EXPECT_EQ(fidl::Reason::kClose, info.reason());
    EXPECT_OK(info.status());
    EXPECT_TRUE(server_end);
    delete server;
  };
  auto binding_ref =
      fidl::BindServer(loop.dispatcher(), std::move(remote), server, std::move(on_unbound));

  // The binding should be destroyed without waiting for the Server to be destroyed.
  binding_ref.Close(ZX_OK);
  ASSERT_OK(local.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr));
  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));

  // Unbind()/Close() may still be called from the Server.
  binding_ref.Unbind();
  binding_ref.Close(ZX_OK);

  // Verify the epitaph from Close().
  fidl_epitaph_t epitaph;
  ASSERT_OK(
      local.channel().read(0, &epitaph, nullptr, sizeof(fidl_epitaph_t), 0, nullptr, nullptr));
  EXPECT_EQ(ZX_OK, epitaph.error);
}

TEST(BindServerTestCase, UnbindInfoChannelError) {
  struct WorkingServer : fidl::WireServer<Simple> {
    WorkingServer() = default;
    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      EXPECT_EQ(ZX_ERR_ACCESS_DENIED, completer.Reply(request->request).status());
    }
    void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
      ADD_FAILURE("Must not call close");
    }
  };

  // Launches a new thread for the server so we can wait on the worker.
  auto server = std::make_unique<WorkingServer>();
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<Simple>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);
  ASSERT_OK(
      remote.channel().replace(ZX_DEFAULT_CHANNEL_RIGHTS & ~ZX_RIGHT_WRITE, &remote.channel()));

  sync_completion_t closed;
  fidl::OnUnboundFn<WorkingServer> on_unbound = [&closed](WorkingServer*, fidl::UnbindInfo info,
                                                          fidl::ServerEnd<Simple>) {
    EXPECT_EQ(fidl::Reason::kTransportError, info.reason());
    EXPECT_EQ(ZX_ERR_ACCESS_DENIED, info.status());
    sync_completion_signal(&closed);
  };
  fidl::BindServer(loop.dispatcher(), std::move(remote), server.get(), std::move(on_unbound));

  auto result = WireCall(local).Echo(kExpectedReply);
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, result.status());

  // Wait for the closed callback to be called.
  ASSERT_OK(sync_completion_wait(&closed, ZX_TIME_INFINITE));
}

TEST(BindServerTestCase, UnbindInfoDispatcherError) {
  // Create the server.
  sync_completion_t destroyed;
  auto* server = new Server(&destroyed);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  // Create and bind the channel.
  auto endpoints = fidl::CreateEndpoints<Simple>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);
  auto remote_handle = remote.channel().get();
  fidl::OnUnboundFn<Server> on_unbound = [remote_handle, remote = &remote](
                                             Server* server, fidl::UnbindInfo info,
                                             fidl::ServerEnd<Simple> server_end) {
    EXPECT_EQ(fidl::Reason::kDispatcherError, info.reason());
    EXPECT_EQ(ZX_ERR_CANCELED, info.status());
    EXPECT_EQ(server_end.channel().get(), remote_handle);
    *remote = std::move(server_end);
    delete server;
  };
  auto binding_ref =
      fidl::BindServer(loop.dispatcher(), std::move(remote), server, std::move(on_unbound));

  // This should destroy the binding, running the error handler before returning.
  loop.Shutdown();
  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE_PAST));

  // The channel should still be valid.
  EXPECT_EQ(remote.channel().get(), remote_handle);

  // No epitaph should have been sent.
  EXPECT_EQ(ZX_ERR_TIMED_OUT,
            local.channel().wait_one(ZX_CHANNEL_READABLE, zx::time::infinite_past(), nullptr));
}

TEST(BindServerTestCase, ReplyNotRequiredAfterUnbound) {
  struct WorkingServer : fidl::WireServer<Simple> {
    explicit WorkingServer(std::optional<EchoCompleter::Async>* async_completer,
                           sync_completion_t* ready)
        : async_completer_(async_completer), ready_(ready) {}
    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      sync_completion_signal(ready_);
      *async_completer_ = completer.ToAsync();  // Releases ownership of the binding.
    }
    void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
      ADD_FAILURE("Must not call close");
    }
    std::optional<EchoCompleter::Async>* async_completer_;
    sync_completion_t* ready_;
  };

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  // Create the channel and bind it with the server and dispatcher.
  auto endpoints = fidl::CreateEndpoints<Simple>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);
  sync_completion_t ready, unbound;
  std::optional<Server::EchoCompleter::Async> async_completer;
  auto server = std::make_unique<WorkingServer>(&async_completer, &ready);
  fidl::OnUnboundFn<WorkingServer> on_unbound = [&unbound](WorkingServer*, fidl::UnbindInfo info,
                                                           fidl::ServerEnd<Simple>) {
    EXPECT_EQ(fidl::Reason::kUnbind, info.reason());
    EXPECT_EQ(ZX_OK, info.status());
    sync_completion_signal(&unbound);
  };
  auto binding_ref =
      fidl::BindServer(loop.dispatcher(), std::move(remote), server.get(), std::move(on_unbound));

  // Start another thread to make the outgoing call.
  std::thread([local = std::move(local)]() mutable {
    auto result = WireCall(local).Echo(kExpectedReply);
    EXPECT_EQ(ZX_ERR_PEER_CLOSED, result.status());
  }).detach();

  // Wait for the server to enter Echo().
  ASSERT_OK(sync_completion_wait(&ready, ZX_TIME_INFINITE));

  // Unbind the server.
  binding_ref.Unbind();

  // Wait for the OnUnboundFn.
  ASSERT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));

  // The AsyncCompleter will be destroyed without having Reply()d or Close()d
  // but should not crash.
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
                               public fidl::WireServer<Simple>,
                               public PlaceholderBase2 {
 public:
  explicit MultiInheritanceServer(sync_completion_t* destroyed) : destroyed_(destroyed) {}
  MultiInheritanceServer(MultiInheritanceServer&& other) = delete;
  MultiInheritanceServer(const MultiInheritanceServer& other) = delete;
  MultiInheritanceServer& operator=(MultiInheritanceServer&& other) = delete;
  MultiInheritanceServer& operator=(const MultiInheritanceServer& other) = delete;

  ~MultiInheritanceServer() override { sync_completion_signal(destroyed_); }

  void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
    completer.Reply(request->request);
  }
  void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
    completer.Close(ZX_OK);
  }

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

  auto endpoints = fidl::CreateEndpoints<Simple>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  fidl::OnUnboundFn<MultiInheritanceServer> on_unbound = [](MultiInheritanceServer* server,
                                                            fidl::UnbindInfo info,
                                                            fidl::ServerEnd<Simple> server_end) {
    EXPECT_EQ(fidl::Reason::kClose, info.reason());
    EXPECT_OK(info.status());
    EXPECT_TRUE(server_end);
    delete server;
  };

  fidl::BindServer(loop.dispatcher(), std::move(remote), new MultiInheritanceServer(&destroyed),
                   std::move(on_unbound));
  ASSERT_FALSE(sync_completion_signaled(&destroyed));

  auto result = WireCall(local).Close();
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

}  // namespace
