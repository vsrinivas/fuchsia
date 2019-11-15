// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl-async/cpp/async_bind.h>
#include <lib/sync/completion.h>
#include <zircon/syscalls.h>

#include <vector>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <zxtest/zxtest.h>

#include "generated/fidl_llcpp_simple.test.h"

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
  fidl::OnChannelCloseFn<SyncServer> on_closing = [](SyncServer* server) {};
  fidl::OnChannelCloseFn<SyncServer> on_closed = [&closed](SyncServer* server) {
    sync_completion_signal(&closed);
  };
  fidl::AsyncBind(loop.dispatcher(), std::move(remote), server.get(), std::move(on_closing),
                  std::move(on_closed));

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
      worker_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);
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
  fidl::OnChannelCloseFn<AsyncServer> on_closing = [](AsyncServer* server) {};
  fidl::OnChannelCloseFn<AsyncServer> on_closed = [&closed](AsyncServer* server) {
    sync_completion_signal(&closed);
  };
  fidl::AsyncBind(main.dispatcher(), std::move(remote), server.get(), std::move(on_closing),
                  std::move(on_closed));

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
      auto worker = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);
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
  fidl::OnChannelCloseFn<AsyncDelayedServer> on_closing = [](AsyncDelayedServer* server) {};
  fidl::OnChannelCloseFn<AsyncDelayedServer> on_closed = [&closed](AsyncDelayedServer* server) {
    sync_completion_signal(&closed);
  };
  fidl::AsyncBind(main.dispatcher(), std::move(remote), server.get(), std::move(on_closing),
                  std::move(on_closed));

  // Sync client calls.
  sync_completion_t done;
  std::vector<std::unique_ptr<async::Loop>> clients;
  for (uint32_t i = 0; i < kNumberOfAsyncs; ++i) {
    auto client = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);
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
      auto worker = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);
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
  fidl::OnChannelCloseFn<AsyncDelayedServer> on_closing = [](AsyncDelayedServer* server) {};
  fidl::OnChannelCloseFn<AsyncDelayedServer> on_closed = [&closed](AsyncDelayedServer* server) {
    sync_completion_signal(&closed);
  };
  fidl::AsyncBind(main.dispatcher(), std::move(remote), server.get(), std::move(on_closing),
                  std::move(on_closed));

  // Sync client calls.
  std::vector<std::unique_ptr<async::Loop>> clients;
  for (uint32_t i = 0; i < kNumberOfAsyncs; ++i) {
    auto client = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);
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

  fidl::OnChannelCloseFn<Server> on_closing = [](Server* server) {};
  fidl::OnChannelCloseFn<Server> on_closed = [](Server* server) { delete server; };

  fidl::AsyncBind(loop.dispatcher(), std::move(remote), server.release(), std::move(on_closing),
                  std::move(on_closed));
  loop.RunUntilIdle();
  ASSERT_FALSE(sync_completion_signaled(&destroyed));

  local.reset();
  loop.RunUntilIdle();
  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));
}

TEST(AsyncBindTestCase, CallbacksClosingAndClosedClientTriggered) {
  struct ClosingServer : ::llcpp::fidl::test::simple::Simple::Interface {
    explicit ClosingServer(sync_completion_t* worker_start, sync_completion_t* worker_done)
        : worker_start_(worker_start), worker_done_(worker_done) {}
    void Echo(int32_t request, EchoCompleter::Sync completer) override {
      // Launches a thread so we can hold the transaction in progress.
      worker_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);
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
  sync_completion_t worker_start, worker_done, closing, closed;

  // Launches a thread so we can wait on the server closing.
  auto server = std::make_unique<ClosingServer>(&worker_start, &worker_done);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  fidl::OnChannelCloseFn<ClosingServer> on_closing = [&closing](ClosingServer* server) {
    sync_completion_signal(&closing);
  };
  fidl::OnChannelCloseFn<ClosingServer> on_closed = [&closed](ClosingServer* server) {
    sync_completion_signal(&closed);
  };

  fidl::AsyncBind<ClosingServer>(loop.dispatcher(), std::move(remote), server.get(),
                                 std::move(on_closing), std::move(on_closed));

  ASSERT_FALSE(sync_completion_signaled(&worker_start));
  ASSERT_FALSE(sync_completion_signaled(&worker_done));
  ASSERT_FALSE(sync_completion_signaled(&closing));
  ASSERT_FALSE(sync_completion_signaled(&closed));

  // Client launches a thread so we can hold the transaction in progress.
  auto client = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);
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

  // Client closes the channel, we are now closing and on_closing is called.
  local.reset();

  // Wait for the closing callback to be called.
  ASSERT_OK(sync_completion_wait(&closing, ZX_TIME_INFINITE));

  // Give some time to the server thread to potentially call close, it should not though.
  // This sleep is not needed to pass the test, it just allows the server to potentially fail.
  zx::nanosleep(zx::deadline_after(zx::msec(100)));

  // Trigger finishing the only outstanding transaction.
  sync_completion_signal(&worker_done);

  // Wait for the closed callback to be called.
  ASSERT_OK(sync_completion_wait(&closed, ZX_TIME_INFINITE));
}

TEST(AsyncBindTestCase, DestroyBindingWithPendingCancel) {
  struct WorkingServer : ::llcpp::fidl::test::simple::Simple::Interface {
    explicit WorkingServer(sync_completion_t* worker_start, sync_completion_t* worker_done)
        : worker_start_(worker_start), worker_done_(worker_done) {}
    void Echo(int32_t request, EchoCompleter::Sync completer) override {
      // Launches a thread so we can hold the transaction.
      worker_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);
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
  sync_completion_t worker_start, worker_done, server_busy;

  // Launches a new thread for the server so we can wait on the worker.
  auto server = std::make_unique<WorkingServer>(&worker_start, &worker_done);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  sync_completion_t closed;
  fidl::OnChannelCloseFn<WorkingServer> on_closing = [](WorkingServer* server) {};
  fidl::OnChannelCloseFn<WorkingServer> on_closed = [&closed](WorkingServer* server) {
    sync_completion_signal(&closed);
  };

  fidl::AsyncBind<WorkingServer>(loop.dispatcher(), std::move(remote), server.get(),
                                 std::move(on_closing), std::move(on_closed));

  ASSERT_FALSE(sync_completion_signaled(&worker_start));
  ASSERT_FALSE(sync_completion_signaled(&worker_done));
  ASSERT_FALSE(sync_completion_signaled(&server_busy));

  // Client launches a thread so we can hold the transaction in progress.
  auto client = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);
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

  // Client closes its end of the channel, we are now closing but can't close until the in-flight
  // transaction is destroyed.
  local.reset();

  // Make the server busy.
  async::PostTask(loop.dispatcher(), [&server_busy]() {
    ASSERT_OK(sync_completion_wait(&server_busy, ZX_TIME_INFINITE));
  });

  // Trigger finishing the transaction, Reply() will fail (closed channel) and the transaction will
  // Close(). We make sure the channel closing by the client happens first and the in-flight
  // transaction tries to Reply() second.
  sync_completion_signal(&worker_done);

  // Wait until after the worker issues its Close().
  server->worker_->JoinThreads();

  // Free up the server so it can now process the Cancel() from the failed transaction. The server
  // must not be destroyed before any post to its thread is completed. An ASAN run catches this if
  // it happens now.
  sync_completion_signal(&server_busy);

  // Give some extra time to the server thread to run after destroyed, it should not though.
  // This sleep is not needed to pass the test, it just allows the server to potentially fail.
  zx::nanosleep(zx::deadline_after(zx::msec(100)));

  // Wait for the closed callback to be called.
  ASSERT_OK(sync_completion_wait(&closed, ZX_TIME_INFINITE));
}

TEST(AsyncBindTestCase, CallbacksClosingAndClosedServerTriggered) {
  struct ClosingServer : ::llcpp::fidl::test::simple::Simple::Interface {
    explicit ClosingServer(sync_completion_t* worker_start, sync_completion_t* worker_done)
        : worker_start_(worker_start), worker_done_(worker_done) {}
    void Echo(int32_t request, EchoCompleter::Sync completer) override {
      // Launches a thread so we can hold the transaction in progress.
      worker_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);
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
  sync_completion_t worker_start, worker_done, closing, closed;

  // Launches a thread so we can wait on the server closing.
  auto server = std::make_unique<ClosingServer>(&worker_start, &worker_done);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  fidl::OnChannelCloseFn<ClosingServer> on_closing = [&closing](ClosingServer* server) {
    sync_completion_signal(&closing);
  };
  fidl::OnChannelCloseFn<ClosingServer> on_closed = [&closed](ClosingServer* server) {
    sync_completion_signal(&closed);
  };

  fidl::AsyncBind<ClosingServer>(loop.dispatcher(), std::move(remote), server.get(),
                                 std::move(on_closing), std::move(on_closed));

  ASSERT_FALSE(sync_completion_signaled(&worker_start));
  ASSERT_FALSE(sync_completion_signaled(&worker_done));
  ASSERT_FALSE(sync_completion_signaled(&closing));
  ASSERT_FALSE(sync_completion_signaled(&closed));

  // Client1 launches a thread so we can hold its transaction in progress.
  auto client1 = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);
  async::PostTask(client1->dispatcher(), [&local]() {
    auto result =
        ::llcpp::fidl::test::simple::Simple::Call::Echo(zx::unowned_channel{local}, kExpectedReply);
    if (result.status() != ZX_ERR_PEER_CLOSED) {
      FAIL();
    }
  });
  ASSERT_OK(client1->StartThread());

  // Wait until worker_start so we have an in-flight transaction.
  ASSERT_OK(sync_completion_wait(&worker_start, ZX_TIME_INFINITE));

  // Client2 launches a thread to continue the test while its transaction is still in progress.
  auto client2 = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);
  async::PostTask(client2->dispatcher(), [&local]() {
    // Server will close the channel, we are now closing and on_closing is called.
    auto result = ::llcpp::fidl::test::simple::Simple::Call::Close(zx::unowned_channel{local});
    if (result.status() != ZX_ERR_PEER_CLOSED) {
      FAIL();
    }
  });
  ASSERT_OK(client2->StartThread());

  // Wait for the closing callback to be called.
  ASSERT_OK(sync_completion_wait(&closing, ZX_TIME_INFINITE));

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

  fidl::OnChannelCloseFn<Server> on_closing = [](Server* server) {};
  fidl::OnChannelCloseFn<Server> on_closed = [](Server* server) { delete server; };

  fidl::AsyncBind(loop.dispatcher(), std::move(remote), server.release(), std::move(on_closing),
                  std::move(on_closed));
  ASSERT_FALSE(sync_completion_signaled(&destroyed));

  auto result = ::llcpp::fidl::test::simple::Simple::Call::Close(zx::unowned_channel{local});
  ASSERT_EQ(result.status(), ZX_ERR_PEER_CLOSED);

  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));
  // Make sure the other end closed
  ASSERT_OK(local.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time{}, nullptr));
}

TEST(AsyncBindTestCase, ExplicitForceSyncUnbind) {
  // Server launches a thread so we can make sync client calls.
  sync_completion_t destroyed;
  auto server = std::make_unique<Server>(&destroyed);
  async::Loop main(&kAsyncLoopConfigNoAttachToCurrentThread);

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  auto binding_ref =
      fidl::BindingRef::CreateAsyncBinding(main.dispatcher(), std::move(remote), std::move(server));

  main.RunUntilIdle();
  ASSERT_TRUE(binding_ref.is_ok());
  binding_ref.value().Unbind();
  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));
}

TEST(AsyncBindTestCase, ExplicitForceSyncUnbindWithPendingTransaction) {
  struct WorkingServer : ::llcpp::fidl::test::simple::Simple::Interface {
    explicit WorkingServer(sync_completion_t* worker_start, sync_completion_t* worker_done)
        : worker_start_(worker_start), worker_done_(worker_done) {}
    void Echo(int32_t request, EchoCompleter::Sync completer) override {
      // Launches a thread so we can hold the transaction.
      worker_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);
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

  // Client launches a thread so we can hold the transaction in progress.
  auto client = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);
  async::PostTask(client->dispatcher(), [&local, client = client.get()]() {
    ::llcpp::fidl::test::simple::Simple::Call::Echo(zx::unowned_channel{local}, kExpectedReply);
  });
  ASSERT_OK(client->StartThread());

  fidl::OnChannelCloseFn<WorkingServer> on_closing = [](WorkingServer* server) {};
  fidl::OnChannelCloseFn<WorkingServer> on_closed = [](WorkingServer* server) {};

  auto binding_ref =
      fidl::BindingRef::CreateAsyncBinding(loop.dispatcher(), std::move(remote), server.get(),
                                           std::move(on_closing), std::move(on_closed));
  ASSERT_TRUE(binding_ref.is_ok());

  // Wait until worker_start so we have an in-flight transaction.
  ASSERT_OK(sync_completion_wait(&worker_start, ZX_TIME_INFINITE));

  async::PostTask(loop.dispatcher(),
                  [binding_ref = std::move(binding_ref), &worker_done]() mutable {
                    binding_ref.value().Unbind();
                    sync_completion_signal(&worker_done);
                  });

  // The server's worker_ destruction is not completed until its loop is destroyed so the test is
  // blocked until worker_done is signalled inside the last post after we test ForceSyncUnbind.
}

}  // namespace
