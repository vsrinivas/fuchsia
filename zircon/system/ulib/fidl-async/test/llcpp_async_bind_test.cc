// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl-async/cpp/async_bind.h>
#include <lib/sync/completion.h>
#include <zircon/syscalls.h>

#include <thread>
#include <vector>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fidl/test/simple/llcpp/fidl.h>
#include <zxtest/zxtest.h>

namespace {

constexpr uint32_t kNumberOfAsyncs = 10;
constexpr int32_t kExpectedReply = 7;

class Server : public ::llcpp::fidl::test::simple::Simple::Interface {
 public:
  explicit Server(sync_completion_t* destroyed) : destroyed_(destroyed) {}
  Server(Server&& other) = delete;
  Server(const Server& other) = delete;
  Server& operator=(Server&& other) = delete;
  Server& operator=(const Server& other) = delete;

  ~Server() override { sync_completion_signal(destroyed_); }

  void Echo(int32_t request, EchoCompleter::Sync completer) override { completer.Reply(request); }
  void Close(CloseCompleter::Sync completer) override { completer.Close(ZX_OK); }

 private:
  sync_completion_t* destroyed_;
};

TEST(AsyncBindTestCase, SyncReply) {
  struct SyncServer : ::llcpp::fidl::test::simple::Simple::Interface {
    void Close(CloseCompleter::Sync completer) override { ADD_FAILURE("Must not call close"); }
    void Echo(int32_t request, EchoCompleter::Sync completer) override { completer.Reply(request); }
  };

  // Server launches a thread so we can make sync client calls.
  auto server = std::make_unique<SyncServer>();
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  sync_completion_t closed;
  fidl::OnUnboundFn<SyncServer> on_unbound = [&closed](SyncServer*, fidl::UnboundReason reason,
                                                       zx::channel channel) {
    ASSERT_EQ(reason, fidl::UnboundReason::kPeerClosed);
    ASSERT_FALSE(channel);
    sync_completion_signal(&closed);
  };
  fidl::AsyncBind(loop.dispatcher(), std::move(remote), server.get(), std::move(on_unbound));

  // Sync client call.
  auto result =
      ::llcpp::fidl::test::simple::Simple::Call::Echo(zx::unowned_channel{local}, kExpectedReply);
  ASSERT_OK(result.status());
  ASSERT_EQ(result->reply, kExpectedReply);

  local.reset();  // To trigger binding destruction before loop's destruction.
  ASSERT_OK(sync_completion_wait(&closed, ZX_TIME_INFINITE));
}

TEST(AsyncBindTestCase, AsyncReply) {
  struct AsyncServer : ::llcpp::fidl::test::simple::Simple::Interface {
    void Close(CloseCompleter::Sync completer) override { ADD_FAILURE("Must not call close"); }
    void Echo(int32_t request, EchoCompleter::Sync completer) override {
      worker_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
      async::PostTask(worker_->dispatcher(), [request, completer = completer.ToAsync()]() mutable {
        completer.Reply(request);
      });
      ASSERT_OK(worker_->StartThread());
    }
    std::unique_ptr<async::Loop> worker_;
  };

  // Server launches a thread so we can make sync client calls.
  auto server = std::make_unique<AsyncServer>();
  async::Loop main(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(main.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  sync_completion_t closed;
  fidl::OnUnboundFn<AsyncServer> on_unbound = [&closed](AsyncServer*, fidl::UnboundReason reason,
                                                        zx::channel channel) {
    ASSERT_EQ(reason, fidl::UnboundReason::kPeerClosed);
    ASSERT_FALSE(channel);
    sync_completion_signal(&closed);
  };
  fidl::AsyncBind(main.dispatcher(), std::move(remote), server.get(), std::move(on_unbound));

  // Sync client call.
  auto result =
      ::llcpp::fidl::test::simple::Simple::Call::Echo(zx::unowned_channel{local}, kExpectedReply);
  ASSERT_OK(result.status());
  ASSERT_EQ(result->reply, kExpectedReply);

  local.reset();  // To trigger binding destruction before main's destruction.
  ASSERT_OK(sync_completion_wait(&closed, ZX_TIME_INFINITE));
}

TEST(AsyncBindTestCase, MultipleAsyncReplies) {
  struct AsyncDelayedServer : ::llcpp::fidl::test::simple::Simple::Interface {
    void Close(CloseCompleter::Sync completer) override { ADD_FAILURE("Must not call close"); }
    void Echo(int32_t request, EchoCompleter::Sync completer) override {
      auto worker = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
      async::PostTask(worker->dispatcher(),
                      [request, completer = completer.ToAsync(), this]() mutable {
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

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  sync_completion_t closed;
  fidl::OnUnboundFn<AsyncDelayedServer> on_unbound =
      [&closed](AsyncDelayedServer* server, fidl::UnboundReason reason, zx::channel channel) {
        ASSERT_EQ(reason, fidl::UnboundReason::kPeerClosed);
        ASSERT_FALSE(channel);
        sync_completion_signal(&closed);
      };
  fidl::AsyncBind(main.dispatcher(), std::move(remote), server.get(), std::move(on_unbound));

  // Sync client calls.
  sync_completion_t done;
  std::vector<std::unique_ptr<async::Loop>> clients;
  for (uint32_t i = 0; i < kNumberOfAsyncs; ++i) {
    auto client = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    async::PostTask(client->dispatcher(), [&local, &done]() {
      auto result = ::llcpp::fidl::test::simple::Simple::Call::Echo(zx::unowned_channel{local},
                                                                    kExpectedReply);
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

TEST(AsyncBindTestCase, MultipleAsyncRepliesOnePeerClose) {
  struct AsyncDelayedServer : ::llcpp::fidl::test::simple::Simple::Interface {
    AsyncDelayedServer(std::vector<std::unique_ptr<async::Loop>>* loops) : loops_(loops) {}
    void Close(CloseCompleter::Sync completer) override { ADD_FAILURE("Must not call close"); }
    void Echo(int32_t request, EchoCompleter::Sync completer) override {
      auto worker = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
      async::PostTask(worker->dispatcher(),
                      [request, completer = completer.ToAsync(), this]() mutable {
                        bool signal = false;
                        static std::atomic<int> count;
                        if (++count == kNumberOfAsyncs) {
                          signal = true;
                        }
                        if (signal) {
                          sync_completion_signal(&done_);
                        }
                        if (signal) {
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

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  sync_completion_t closed;
  fidl::OnUnboundFn<AsyncDelayedServer> on_unbound =
      [&closed](AsyncDelayedServer*, fidl::UnboundReason reason, zx::channel channel) {
        ASSERT_EQ(reason, fidl::UnboundReason::kUnbind);
        ASSERT_FALSE(channel);
        sync_completion_signal(&closed);
      };
  fidl::AsyncBind(main.dispatcher(), std::move(remote), server.get(), std::move(on_unbound));

  // Sync client calls.
  std::vector<std::unique_ptr<async::Loop>> clients;
  for (uint32_t i = 0; i < kNumberOfAsyncs; ++i) {
    auto client = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    async::PostTask(client->dispatcher(), [&local, client = client.get()]() {
      auto result = ::llcpp::fidl::test::simple::Simple::Call::Echo(zx::unowned_channel{local},
                                                                    kExpectedReply);
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
}

TEST(AsyncBindTestCase, UniquePtrDestroyOnClientClose) {
  sync_completion_t destroyed;
  auto server = std::make_unique<Server>(&destroyed);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  fidl::AsyncBind(loop.dispatcher(), std::move(remote), std::move(server));
  loop.RunUntilIdle();
  ASSERT_FALSE(sync_completion_signaled(&destroyed));

  local.reset();
  loop.RunUntilIdle();
  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));
}

TEST(AsyncBindTestCase, UniquePtrDestroyOnServerClose) {
  sync_completion_t destroyed;
  // Server launches a thread so we can make sync client calls.
  auto server = std::make_unique<Server>(&destroyed);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  fidl::AsyncBind(loop.dispatcher(), std::move(remote), std::move(server));
  ASSERT_FALSE(sync_completion_signaled(&destroyed));

  auto result = ::llcpp::fidl::test::simple::Simple::Call::Close(zx::unowned_channel{local});
  ASSERT_EQ(result.status(), ZX_ERR_PEER_CLOSED);
  // Make sure the other end closed.
  ASSERT_OK(local.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time{}, nullptr));
  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));
}

TEST(AsyncBindTestCase, CallbackDestroyOnClientClose) {
  sync_completion_t destroyed;
  auto server = std::make_unique<Server>(&destroyed);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  fidl::OnUnboundFn<Server> on_unbound = [](Server* server, fidl::UnboundReason reason,
                                            zx::channel channel) {
    ASSERT_EQ(reason, fidl::UnboundReason::kPeerClosed);
    ASSERT_FALSE(channel);
    delete server;
  };

  fidl::AsyncBind(loop.dispatcher(), std::move(remote), server.release(), std::move(on_unbound));
  loop.RunUntilIdle();
  ASSERT_FALSE(sync_completion_signaled(&destroyed));

  local.reset();
  loop.RunUntilIdle();
  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));
}

TEST(AsyncBindTestCase, CallbackErrorClientTriggered) {
  struct ErrorServer : ::llcpp::fidl::test::simple::Simple::Interface {
    explicit ErrorServer(sync_completion_t* worker_start, sync_completion_t* worker_done)
        : worker_start_(worker_start), worker_done_(worker_done) {}
    void Echo(int32_t request, EchoCompleter::Sync completer) override {
      // Launches a thread so we can hold the transaction in progress.
      worker_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
      async::PostTask(worker_->dispatcher(),
                      [request, completer = completer.ToAsync(), this]() mutable {
                        sync_completion_signal(worker_start_);
                        sync_completion_wait(worker_done_, ZX_TIME_INFINITE);
                        completer.Reply(request);
                      });
      ASSERT_OK(worker_->StartThread());
    }
    void Close(CloseCompleter::Sync completer) override { ADD_FAILURE("Must not call close"); }
    sync_completion_t* worker_start_;
    sync_completion_t* worker_done_;
    std::unique_ptr<async::Loop> worker_;
  };
  sync_completion_t worker_start, worker_done, error, closed;

  // Launches a thread so we can wait on the server error.
  auto server = std::make_unique<ErrorServer>(&worker_start, &worker_done);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  fidl::OnUnboundFn<ErrorServer> on_unbound = [&error](ErrorServer*, fidl::UnboundReason reason,
                                                       zx::channel channel) {
    ASSERT_EQ(reason, fidl::UnboundReason::kPeerClosed);
    ASSERT_FALSE(channel);
    sync_completion_signal(&error);
  };

  fidl::AsyncBind<ErrorServer>(loop.dispatcher(), std::move(remote), server.get(),
                               std::move(on_unbound));

  ASSERT_FALSE(sync_completion_signaled(&worker_start));
  ASSERT_FALSE(sync_completion_signaled(&worker_done));
  ASSERT_FALSE(sync_completion_signaled(&error));

  // Client launches a thread so we can hold the transaction in progress.
  auto client = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  async::PostTask(client->dispatcher(), [&local, client = client.get()]() {
    auto result =
        ::llcpp::fidl::test::simple::Simple::Call::Echo(zx::unowned_channel{local}, kExpectedReply);
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

TEST(AsyncBindTestCase, DestroyBindingWithPendingCancel) {
  struct WorkingServer : ::llcpp::fidl::test::simple::Simple::Interface {
    explicit WorkingServer(sync_completion_t* worker_start, sync_completion_t* worker_done)
        : worker_start_(worker_start), worker_done_(worker_done) {}
    void Echo(int32_t request, EchoCompleter::Sync completer) override {
      // Launches a thread so we can hold the transaction.
      worker_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
      async::PostTask(worker_->dispatcher(), [request, completer = completer.ToAsync(), this,
                                              worker = worker_.get()]() mutable {
        sync_completion_signal(worker_start_);
        sync_completion_wait(worker_done_, ZX_TIME_INFINITE);
        completer.Reply(request);
        worker->Quit();
      });
      ASSERT_OK(worker_->StartThread());
    }
    void Close(CloseCompleter::Sync completer) override { ADD_FAILURE("Must not call close"); }
    sync_completion_t* worker_start_;
    sync_completion_t* worker_done_;
    std::unique_ptr<async::Loop> worker_;
  };
  sync_completion_t worker_start, worker_done;

  // Launches a new thread for the server so we can wait on the worker.
  auto server = std::make_unique<WorkingServer>(&worker_start, &worker_done);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  sync_completion_t closed;
  fidl::OnUnboundFn<WorkingServer> on_unbound =
      [&closed](WorkingServer*, fidl::UnboundReason reason, zx::channel channel) {
        ASSERT_EQ(reason, fidl::UnboundReason::kPeerClosed);
        ASSERT_FALSE(channel);
        sync_completion_signal(&closed);
      };

  fidl::AsyncBind<WorkingServer>(loop.dispatcher(), std::move(remote), server.get(),
                                 std::move(on_unbound));

  ASSERT_FALSE(sync_completion_signaled(&worker_start));
  ASSERT_FALSE(sync_completion_signaled(&worker_done));
  ASSERT_FALSE(sync_completion_signaled(&closed));

  // Client launches a thread so we can hold the transaction in progress.
  auto client = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  async::PostTask(client->dispatcher(), [&local, client = client.get()]() {
    auto result =
        ::llcpp::fidl::test::simple::Simple::Call::Echo(zx::unowned_channel{local}, kExpectedReply);
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

  // Wait until after the worker issues its Close().
  server->worker_->JoinThreads();

  // Wait for the closed callback to be called.
  ASSERT_OK(sync_completion_wait(&closed, ZX_TIME_INFINITE));
}

TEST(AsyncBindTestCase, CallbackErrorServerTriggered) {
  struct ErrorServer : ::llcpp::fidl::test::simple::Simple::Interface {
    explicit ErrorServer(sync_completion_t* worker_start, sync_completion_t* worker_done)
        : worker_start_(worker_start), worker_done_(worker_done) {}
    void Echo(int32_t request, EchoCompleter::Sync completer) override {
      // Launches a thread so we can hold the transaction in progress.
      worker_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
      async::PostTask(worker_->dispatcher(),
                      [request, completer = completer.ToAsync(), this]() mutable {
                        sync_completion_signal(worker_start_);
                        sync_completion_wait(worker_done_, ZX_TIME_INFINITE);
                        completer.Reply(request);
                      });
      ASSERT_OK(worker_->StartThread());
    }
    void Close(CloseCompleter::Sync completer) override { completer.Close(ZX_ERR_INTERNAL); }
    sync_completion_t* worker_start_;
    sync_completion_t* worker_done_;
    std::unique_ptr<async::Loop> worker_;
  };
  sync_completion_t worker_start, worker_done, closed;

  // Launches a thread so we can wait on the server error.
  auto server = std::make_unique<ErrorServer>(&worker_start, &worker_done);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  fidl::OnUnboundFn<ErrorServer> on_unbound = [&closed](ErrorServer*, fidl::UnboundReason,
                                                        zx::channel channel) {
    ASSERT_FALSE(channel);
    sync_completion_signal(&closed);
  };

  fidl::AsyncBind<ErrorServer>(loop.dispatcher(), std::move(remote), server.get(),
                               std::move(on_unbound));

  ASSERT_FALSE(sync_completion_signaled(&worker_start));
  ASSERT_FALSE(sync_completion_signaled(&worker_done));
  ASSERT_FALSE(sync_completion_signaled(&closed));

  // Client1 launches a thread so we can hold its transaction in progress.
  auto client1 = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  async::PostTask(client1->dispatcher(), [&local]() {
    ::llcpp::fidl::test::simple::Simple::Call::Echo(zx::unowned_channel{local}, kExpectedReply);
  });
  ASSERT_OK(client1->StartThread());

  // Wait until worker_start so we have an in-flight transaction.
  ASSERT_OK(sync_completion_wait(&worker_start, ZX_TIME_INFINITE));

  // Client2 launches a thread to continue the test while its transaction is still in progress.
  auto client2 = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  async::PostTask(client2->dispatcher(), [&local]() {
    // Server will close the channel, on_unbound is not called.
    auto result = ::llcpp::fidl::test::simple::Simple::Call::Close(zx::unowned_channel{local});
    if (result.status() != ZX_ERR_PEER_CLOSED) {
      FAIL();
    }
  });
  ASSERT_OK(client2->StartThread());

  // Trigger finishing the client1 outstanding transaction.
  sync_completion_signal(&worker_done);

  // Wait for the closed callback to be called.
  ASSERT_OK(sync_completion_wait(&closed, ZX_TIME_INFINITE));
}

TEST(AsyncBindTestCase, CallbackDestroyOnServerClose) {
  sync_completion_t destroyed;
  // Server launches a thread so we can make sync client calls.
  auto server = std::make_unique<Server>(&destroyed);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  fidl::OnUnboundFn<Server> on_unbound = [](Server* server, fidl::UnboundReason,
                                            zx::channel channel) {
    ASSERT_FALSE(channel);
    delete server;
  };

  fidl::AsyncBind(loop.dispatcher(), std::move(remote), server.release(), std::move(on_unbound));
  ASSERT_FALSE(sync_completion_signaled(&destroyed));

  auto result = ::llcpp::fidl::test::simple::Simple::Call::Close(zx::unowned_channel{local});
  ASSERT_EQ(result.status(), ZX_ERR_PEER_CLOSED);

  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));
  // Make sure the other end closed
  ASSERT_OK(local.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time{}, nullptr));
}

TEST(AsyncBindTestCase, ExplicitUnbind) {
  // Server launches a thread so we can make sync client calls.
  sync_completion_t destroyed;
  auto server = std::make_unique<Server>(&destroyed);
  async::Loop main(&kAsyncLoopConfigNoAttachToCurrentThread);

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  auto remote_handle = remote.get();

  fidl::OnUnboundFn<Server> on_unbound = [remote_handle](Server*, fidl::UnboundReason reason,
                                                         zx::channel channel) {
    ASSERT_EQ(reason, fidl::UnboundReason::kUnbind);
    ASSERT_EQ(channel.get(), remote_handle);
    channel.reset();
  };
  auto binding_ref = fidl::BindingRef::CreateAsyncBinding(main.dispatcher(), std::move(remote),
                                                          server.get(), std::move(on_unbound));
  ASSERT_TRUE(binding_ref.is_ok());

  main.RunUntilIdle();
  ASSERT_TRUE(binding_ref.is_ok());
  binding_ref.value().Unbind();
}

TEST(AsyncBindTestCase, ExplicitUnbindWithPendingTransaction) {
  struct WorkingServer : ::llcpp::fidl::test::simple::Simple::Interface {
    explicit WorkingServer(sync_completion_t* worker_start, sync_completion_t* worker_done)
        : worker_start_(worker_start), worker_done_(worker_done) {}
    void Echo(int32_t request, EchoCompleter::Sync completer) override {
      sync_completion_signal(worker_start_);
      sync_completion_wait(worker_done_, ZX_TIME_INFINITE);
      completer.Reply(request);
    }
    void Close(CloseCompleter::Sync completer) override { ADD_FAILURE("Must not call close"); }
    sync_completion_t* worker_start_;
    sync_completion_t* worker_done_;
  };
  sync_completion_t worker_start, worker_done;

  // Launches a new thread for the server so we can wait on the worker.
  auto server = std::make_unique<WorkingServer>(&worker_start, &worker_done);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  zx_handle_t remote_handle = remote.get();

  // Client launches a thread so we can hold the transaction in progress.
  auto client = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  async::PostTask(client->dispatcher(), [&local, client = client.get()]() {
    ::llcpp::fidl::test::simple::Simple::Call::Echo(zx::unowned_channel{local}, kExpectedReply);
  });
  ASSERT_OK(client->StartThread());

  fidl::OnUnboundFn<WorkingServer> on_unbound =
      [remote_handle](WorkingServer*, fidl::UnboundReason reason, zx::channel channel) {
        ASSERT_EQ(reason, fidl::UnboundReason::kUnbind);
        ASSERT_EQ(channel.get(), remote_handle);
        channel.reset();  // Release the handle to trigger ZX_ERR_PEER_CLOSED on the client.
      };
  auto binding_ref = fidl::BindingRef::CreateAsyncBinding(loop.dispatcher(), std::move(remote),
                                                          server.get(), std::move(on_unbound));
  ASSERT_TRUE(binding_ref.is_ok());

  // Wait until worker_start so we have an in-flight transaction.
  ASSERT_OK(sync_completion_wait(&worker_start, ZX_TIME_INFINITE));

  // Unbind the server end of the channel.
  binding_ref.value().Unbind();

  // `loop` will not be destroyed until the thread inside Echo() returns.
  sync_completion_signal(&worker_done);
}

TEST(AsyncBindTestCase, ConcurrentSyncReply) {
  struct ConcurrentSyncServer : ::llcpp::fidl::test::simple::Simple::Interface {
    ConcurrentSyncServer(int max_reqs) : max_reqs_(max_reqs) {}
    void Close(CloseCompleter::Sync completer) override { ADD_FAILURE("Must not call close"); }
    void Echo(int32_t request, EchoCompleter::Sync completer) override {
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
      completer.Reply(request);
    }
    sync_completion_t on_max_reqs_;
    const int max_reqs_;
    std::atomic<int> req_cnt_ = 0;
  };

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  // Launch server with 10 threads.
  constexpr int kMaxReqs = 10;
  auto server = std::make_unique<ConcurrentSyncServer>(kMaxReqs);
  async::Loop server_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  for (int i = 0; i < kMaxReqs; ++i)
    ASSERT_OK(server_loop.StartThread());

  // Bind the server.
  fidl::AsyncBind(server_loop.dispatcher(), std::move(remote), std::move(server));

  // Launch 10 client threads to make two-way Echo() calls.
  std::vector<std::thread> threads;
  for (int i = 0; i < kMaxReqs; ++i) {
    threads.emplace_back([&] {
      auto result = ::llcpp::fidl::test::simple::Simple::Call::Echo(zx::unowned_channel{local},
                                                                    kExpectedReply);
      ASSERT_EQ(result.status(), ZX_OK);
    });
  }

  // Join the client threads.
  for (auto& thread : threads)
    thread.join();
}

TEST(AsyncBindTestCase, ConcurrentIdempotentClose) {
  struct ConcurrentSyncServer : ::llcpp::fidl::test::simple::Simple::Interface {
    void Close(CloseCompleter::Sync completer) override {
      // Add the wait back to the dispatcher. Sleep to allow another thread in.
      completer.EnableNextDispatch();
      zx_nanosleep(0);
      // Close with ZX_OK.
      completer.Close(ZX_OK);
    }
    void Echo(int32_t, EchoCompleter::Sync) override { ADD_FAILURE("Must not call echo"); }
  };

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  // Launch server with 10 threads.
  constexpr int kMaxReqs = 10;
  auto server = std::make_unique<ConcurrentSyncServer>();
  async::Loop server_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  for (int i = 0; i < kMaxReqs; ++i)
    ASSERT_OK(server_loop.StartThread());

  // Bind the server.
  fidl::OnUnboundFn<ConcurrentSyncServer> on_unbound =
      [](ConcurrentSyncServer*, fidl::UnboundReason reason, zx::channel channel) {
        static std::atomic_flag invoked = ATOMIC_FLAG_INIT;
        ASSERT_FALSE(invoked.test_and_set());  // Must only be called once.
        ASSERT_EQ(fidl::UnboundReason::kUnbind, reason);
        ASSERT_FALSE(channel);
      };
  fidl::AsyncBind(server_loop.dispatcher(), std::move(remote), server.get(), std::move(on_unbound));

  // Launch 10 client threads to make two-way Echo() calls.
  std::vector<std::thread> threads;
  for (int i = 0; i < kMaxReqs; ++i) {
    threads.emplace_back([&] {
      auto result = ::llcpp::fidl::test::simple::Simple::Call::Close(zx::unowned_channel{local});
      ASSERT_EQ(result.status(), ZX_ERR_PEER_CLOSED);
    });
  }

  // Join the client threads.
  for (auto& thread : threads)
    thread.join();
}

TEST(AsyncBindTestCase, UnbindBeforeClose) {
  struct CloseServer : ::llcpp::fidl::test::simple::Simple::Interface {
    void Close(CloseCompleter::Sync completer) override {
      binding_ref->Unbind();
      completer.Close(ZX_OK);
    }
    void Echo(int32_t, EchoCompleter::Sync) override { ADD_FAILURE("Must not call echo"); }
    std::unique_ptr<fidl::BindingRef> binding_ref;
  };

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  auto remote_handle = remote.get();

  // Launch server.
  auto server = std::make_unique<CloseServer>();
  async::Loop server_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(server_loop.StartThread());

  // Bind the channel.
  fidl::OnUnboundFn<CloseServer> on_unbound =
      [remote_handle](CloseServer*, fidl::UnboundReason reason, zx::channel channel) {
        ASSERT_EQ(fidl::UnboundReason::kUnbind, reason);
        // Unbind() precedes Close(), so the channel should be valid.
        ASSERT_EQ(remote_handle, channel.get());
        channel.reset();
      };
  auto binding_ref = fidl::BindingRef::CreateAsyncBinding(
      server_loop.dispatcher(), std::move(remote), server.get(), std::move(on_unbound));
  ASSERT_TRUE(binding_ref.is_ok());

  // Give the BindingRef to the server so it can call Unbind().
  server->binding_ref = std::make_unique<fidl::BindingRef>(std::move(binding_ref.value()));

  auto result = ::llcpp::fidl::test::simple::Simple::Call::Close(zx::unowned_channel{local});
  ASSERT_EQ(result.status(), ZX_ERR_PEER_CLOSED);
}

TEST(AsyncBindTestCase, CloseBeforeUnbind) {
  struct UnbindServer : ::llcpp::fidl::test::simple::Simple::Interface {
    void Close(CloseCompleter::Sync completer) override {
      completer.Close(ZX_OK);
      binding_ref->Unbind();
    }
    void Echo(int32_t, EchoCompleter::Sync) override { ADD_FAILURE("Must not call echo"); }
    std::unique_ptr<fidl::BindingRef> binding_ref;
  };

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  // Launch server.
  auto server = std::make_unique<UnbindServer>();
  async::Loop server_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(server_loop.StartThread());

  // Bind the channel.
  fidl::OnUnboundFn<UnbindServer> on_unbound = [](UnbindServer*, fidl::UnboundReason reason,
                                                  zx::channel channel) {
    ASSERT_EQ(fidl::UnboundReason::kUnbind, reason);
    // Close() precedes Unbind(), so the channel will have been closed.
    ASSERT_FALSE(channel);
  };
  auto binding_ref = fidl::BindingRef::CreateAsyncBinding(
      server_loop.dispatcher(), std::move(remote), server.get(), std::move(on_unbound));
  ASSERT_TRUE(binding_ref.is_ok());

  // Give the BindingRef to the server so it can call Unbind().
  server->binding_ref = std::make_unique<fidl::BindingRef>(std::move(binding_ref.value()));

  auto result = ::llcpp::fidl::test::simple::Simple::Call::Close(zx::unowned_channel{local});
  ASSERT_EQ(result.status(), ZX_ERR_PEER_CLOSED);
}

}  // namespace
