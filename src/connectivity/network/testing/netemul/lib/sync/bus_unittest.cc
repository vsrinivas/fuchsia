// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/sys/cpp/testing/test_with_environment.h>

#include "sync_manager.h"

#define ASSERT_OK(st) ASSERT_EQ(ZX_OK, (st))
#define ASSERT_NOK(st) ASSERT_NE(ZX_OK, (st))

#define WAIT_FOR_OK(ok) RunLoopUntil([&ok]() { return ok; })
#define WAIT_FOR_OK_AND_RESET(ok) \
  WAIT_FOR_OK(ok);                \
  ok = false

namespace netemul {
namespace testing {

using sys::testing::EnclosingEnvironment;
using sys::testing::EnvironmentServices;
using sys::testing::TestWithEnvironment;

static const char* kMainTestBus = "test-bus";
static const char* kAltTestBus = "alt-bus";

class BusTest : public TestWithEnvironment {
 public:
  using SyncManagerSync = fidl::SynchronousInterfacePtr<SyncManager::FSyncManager>;
  using BusSync = fidl::SynchronousInterfacePtr<Bus::FBus>;
  using BusAsync = fidl::InterfacePtr<Bus::FBus>;

 protected:
  void SetUp() override {
    fuchsia::sys::EnvironmentPtr parent_env;
    real_services()->Connect(parent_env.NewRequest());

    svc_loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    ASSERT_OK(svc_loop_->StartThread("testloop"));
    svc_ = std::make_unique<SyncManager>(svc_loop_->dispatcher());

    auto services = EnvironmentServices::Create(parent_env, svc_loop_->dispatcher());

    services->AddService(svc_->GetHandler());
    test_env_ = CreateNewEnclosingEnvironment("env", std::move(services));

    WaitForEnclosingEnvToStart(test_env_.get());
  }

  void TearDown() override {
    async::PostTask(svc_loop_->dispatcher(), [this]() {
      svc_.reset();
      svc_loop_->Quit();
    });
    svc_loop_->JoinThreads();
  }

  void GetSyncManager(fidl::InterfaceRequest<SyncManager::FSyncManager> manager) {
    test_env_->ConnectToService(std::move(manager));
  }

  void FillEventData(Bus::FEvent* event, int32_t code = 0, const std::string& name = "",
                     std::vector<uint8_t> args = {}) {
    if (code != 0) {
      event->set_code(code);
    } else {
      event->clear_code();
    }

    if (!name.empty()) {
      event->set_message(name);
    } else {
      event->clear_message();
    }

    if (!args.empty()) {
      event->set_arguments(std::move(args));
    } else {
      event->clear_arguments();
    }
  }

  static bool EventEquals(const netemul::Bus::FEvent& e1, const netemul::Bus::FEvent& e2) {
    return (e1.has_code() == e2.has_code() && (!e1.has_code() || e1.code() == e2.code())) &&
           (e1.has_message() == e2.has_message() &&
            (!e1.has_message() || e1.message() == e2.message())) &&
           (e1.has_arguments() == e2.has_arguments() &&
            (!e1.has_arguments() ||
             (e1.arguments().size() == e2.arguments().size() &&
              memcmp(e1.arguments().data(), e2.arguments().data(), e1.arguments().size()) == 0)));
  }

  void DataExchangeTest(const Bus::FEvent& ref_event_1, const Bus::FEvent& ref_event_2,
                        bool ensure = false) {
    SyncManagerSync bm;
    GetSyncManager(bm.NewRequest());

    BusAsync cli1;
    ASSERT_OK(bm->BusSubscribe(kMainTestBus, "cli1", cli1.NewRequest()));
    ASSERT_TRUE(cli1.is_bound());

    BusAsync cli2;
    ASSERT_OK(bm->BusSubscribe(kMainTestBus, "cli2", cli2.NewRequest()));
    ASSERT_TRUE(cli2.is_bound());

    bool ok1 = false;
    bool ok2 = false;
    cli1.events().OnBusData = [&ok1, &ref_event_1](Bus::FEvent event) {
      ASSERT_TRUE(EventEquals(ref_event_1, event));
      ok1 = true;
    };
    cli2.events().OnBusData = [&ok2, &ref_event_2](Bus::FEvent event) {
      ASSERT_TRUE(EventEquals(ref_event_2, event));
      ok2 = true;
    };

    Bus::FEvent snd;
    ASSERT_OK(ref_event_1.Clone(&snd));
    if (ensure) {
      bool published = false;
      cli2->EnsurePublish(std::move(snd), [&published]() { published = true; });
      WAIT_FOR_OK(published);
    } else {
      cli2->Publish(std::move(snd));
    }
    // wait for client 1 to receive data
    WAIT_FOR_OK_AND_RESET(ok1);
    // client2 mustn't have received anything
    ASSERT_FALSE(ok2);

    ASSERT_OK(ref_event_2.Clone(&snd));
    if (ensure) {
      bool published = false;
      cli1->EnsurePublish(std::move(snd), [&published]() { published = true; });
      WAIT_FOR_OK(published);
    } else {
      cli1->Publish(std::move(snd));
    }
    // wait for client 2 to receive data
    WAIT_FOR_OK_AND_RESET(ok2);
    // client1 mustn't have received anything
    ASSERT_FALSE(ok1);
  }

  bool VectorSetEquals(const std::vector<std::string>& s1, const std::vector<std::string>& s2) {
    if (s1.size() != s2.size()) {
      return false;
    }
    // these vectors are  small enough that the O(n2) search here is not too
    // problematic shouldn't use larger vectors here, performance will suffer
    for (const auto& x1 : s1) {
      bool found = false;
      for (const auto& x2 : s2) {
        if (x2 == x1) {
          found = true;
          break;
        }
      }
      if (!found) {
        return false;
      }
    }
    return true;
  }

  void TestEventWaiting(BusSync* c1, BusAsync* c2, bool expect_result, Bus::FEvent wait,
                        Bus::FEvent publish, const std::string& test_caller) {
    int64_t timeout = zx::msec(expect_result ? 0 : 15).to_nsecs();
    bool got_callback = false;
    (*c2)->WaitForEvent(std::move(wait), timeout,
                        [&got_callback, expect_result, test_caller](bool result) {
                          EXPECT_EQ(result, expect_result) << test_caller;
                          got_callback = true;
                        });
    bool published = false;
    // send whatever on the bus to make sure wait is in effect.
    (*c2)->EnsurePublish(Bus::FEvent(), [&published]() { published = true; });
    WAIT_FOR_OK(published);
    ASSERT_OK((*c1)->EnsurePublish(std::move(publish)));
    WAIT_FOR_OK(got_callback);
  }

  std::unique_ptr<EnclosingEnvironment> test_env_;
  std::unique_ptr<async::Loop> svc_loop_;
  std::unique_ptr<SyncManager> svc_;
};

TEST_F(BusTest, CreateBusAndClient) {
  SyncManagerSync bm;
  GetSyncManager(bm.NewRequest());

  BusSync cli1;
  ASSERT_OK(bm->BusSubscribe(kMainTestBus, "cli1", cli1.NewRequest()));
  ASSERT_TRUE(cli1.is_bound());

  BusSync cli2;
  ASSERT_OK(bm->BusSubscribe(kMainTestBus, "cli2", cli2.NewRequest()));
  ASSERT_TRUE(cli2.is_bound());

  // client with name cli2 on same bus should be disallowed:
  BusSync cli3;
  ASSERT_OK(bm->BusSubscribe(kMainTestBus, "cli2", cli2.NewRequest()));
  RunLoopUntilIdle();
  ASSERT_FALSE(cli3.is_bound());
}

TEST_F(BusTest, ExchangeFullData) {
  Bus::FEvent ref_event_1, ref_event_2;
  FillEventData(&ref_event_1, 1, "Hello evt 1", {1, 2, 3, 4});
  FillEventData(&ref_event_2, 2, "Hello evt 2", {1, 2, 3, 4, 5, 6, 7, 8});

  DataExchangeTest(ref_event_1, ref_event_2);
}

TEST_F(BusTest, ExchangeFullDataEnsured) {
  Bus::FEvent ref_event_1, ref_event_2;
  FillEventData(&ref_event_1, 1, "Hello evt 1", {1, 2, 3, 4});
  FillEventData(&ref_event_2, 2, "Hello evt 2", {1, 2, 3, 4, 5, 6, 7, 8});

  DataExchangeTest(ref_event_1, ref_event_2, true);
}

TEST_F(BusTest, ExchangeCodeOnlyData) {
  Bus::FEvent ref_event_1, ref_event_2;
  FillEventData(&ref_event_1, 1);
  FillEventData(&ref_event_2, 2);

  DataExchangeTest(ref_event_1, ref_event_2);
}

TEST_F(BusTest, CrossTalk) {
  SyncManagerSync bm;
  GetSyncManager(bm.NewRequest());

  bool received_data = false;
  // attach a client to an alternate test bus
  BusAsync cli1;
  ASSERT_OK(bm->BusSubscribe(kAltTestBus, "cli1", cli1.NewRequest()));
  ASSERT_TRUE(cli1.is_bound());
  cli1.events().OnBusData = [&received_data](Bus::FEvent event) { received_data = true; };

  // run a regular data exchange test:
  Bus::FEvent ref_event_1, ref_event_2;
  FillEventData(&ref_event_1, 1);
  FillEventData(&ref_event_2, 2);
  DataExchangeTest(ref_event_1, ref_event_2);
  // ensure that client on opposite bus is still bound and data was not
  // received:
  ASSERT_FALSE(received_data);
  ASSERT_TRUE(cli1.is_bound());
}

TEST_F(BusTest, ClientObservation) {
  SyncManagerSync bm;
  GetSyncManager(bm.NewRequest());

  BusSync cli3;
  ASSERT_OK(bm->BusSubscribe(kMainTestBus, "c3", cli3.NewRequest()));
  std::vector<std::string> clients;
  ASSERT_OK(cli3->GetClients(&clients));
  ASSERT_TRUE(VectorSetEquals(clients, {"c3"}));

  BusAsync cli1;
  ASSERT_OK(bm->BusSubscribe(kMainTestBus, "c1", cli1.NewRequest()));
  ASSERT_TRUE(cli1.is_bound());

  bool ok = false;
  cli1.events().OnClientAttached = [&ok](fidl::StringPtr client) {
    if (client == "c2") {
      ok = true;
    } else {
      ASSERT_EQ(client, "c3");
    }
  };
  cli1.events().OnClientDetached = [&ok](fidl::StringPtr client) {
    ASSERT_EQ(client, "c2");
    ok = true;
  };

  ASSERT_OK(cli3->GetClients(&clients));
  ASSERT_TRUE(VectorSetEquals(clients, {"c1", "c3"}));

  {
    BusAsync cli2;
    ASSERT_OK(bm->BusSubscribe(kMainTestBus, "c2", cli2.NewRequest()));
    ASSERT_TRUE(cli2.is_bound());

    // wait for OnClientAttached event to fire
    WAIT_FOR_OK_AND_RESET(ok);

    ASSERT_OK(cli3->GetClients(&clients));
    ASSERT_TRUE(VectorSetEquals(clients, {"c1", "c2", "c3"}));
  }
  // cli2 went away, wait for client detached event
  WAIT_FOR_OK_AND_RESET(ok);

  // check again that it went away
  ASSERT_OK(cli3->GetClients(&clients));
  ASSERT_TRUE(VectorSetEquals(clients, {"c1", "c3"}));

  // make sure to unbind cli1 first so we don't get the detaching event of cli3
  cli1.Unbind();
}

TEST_F(BusTest, WaitForClients) {
  SyncManagerSync bm;
  GetSyncManager(bm.NewRequest());

  BusSync cli1;
  ASSERT_OK(bm->BusSubscribe(kMainTestBus, "c1", cli1.NewRequest()));

  BusAsync cli_async;
  ASSERT_OK(bm->BusSubscribe(kMainTestBus, "async", cli_async.NewRequest()));

  bool got_callback = false;
  cli_async->WaitForClients({"c1"}, zx::msec(0).to_nsecs(),
                            [&got_callback](bool result, fidl::VectorPtr<std::string> absent) {
                              EXPECT_TRUE(result);
                              EXPECT_FALSE(absent.has_value());
                              got_callback = true;
                            });
  WAIT_FOR_OK_AND_RESET(got_callback);

  cli_async->WaitForClients({"doesn't exist"}, zx::msec(10).to_nsecs(),
                            [&got_callback](bool result, fidl::VectorPtr<std::string> absent) {
                              EXPECT_FALSE(result);
                              ASSERT_TRUE(absent.has_value());
                              ASSERT_EQ(absent->size(), 1ul);
                              EXPECT_EQ(absent->at(0), "doesn't exist");
                              got_callback = true;
                            });
  WAIT_FOR_OK_AND_RESET(got_callback);

  cli_async->WaitForClients({"c1", "c2"}, zx::msec(1000).to_nsecs(),
                            [&got_callback](bool result, fidl::VectorPtr<std::string> absent) {
                              EXPECT_TRUE(result);
                              EXPECT_FALSE(absent.has_value());
                              got_callback = true;
                            });
  RunLoopUntilIdle();
  EXPECT_FALSE(got_callback);
  BusSync cli2;
  ASSERT_OK(bm->BusSubscribe(kMainTestBus, "c2", cli2.NewRequest()));

  WAIT_FOR_OK_AND_RESET(got_callback);
}

TEST_F(BusTest, DestroyWithClientWaiting) {
  SyncManagerSync bm;
  GetSyncManager(bm.NewRequest());

  BusAsync cli_async;
  ASSERT_OK(bm->BusSubscribe(kMainTestBus, "async", cli_async.NewRequest()));

  cli_async->WaitForClients(
      {"c1"}, zx::msec(0).to_nsecs(),
      [](bool result, fidl::VectorPtr<std::string> absent) { FAIL() << "Mustn't reach callback"; });
}

TEST_F(BusTest, WaitForEvent) {
  SyncManagerSync bm;
  GetSyncManager(bm.NewRequest());

  BusSync cli1;
  ASSERT_OK(bm->BusSubscribe(kMainTestBus, "c1", cli1.NewRequest()));

  BusAsync cli_async;
  ASSERT_OK(bm->BusSubscribe(kMainTestBus, "async", cli_async.NewRequest()));

  bool got_callback = false;
  bool published = false;
  Bus::FEvent e1;
  // check that timeout fires
  cli_async->WaitForEvent(std::move(e1), zx::msec(1).to_nsecs(), [&got_callback](bool result) {
    EXPECT_FALSE(result);
    got_callback = true;
  });
  WAIT_FOR_OK_AND_RESET(got_callback);

  int evt_counter = 0;
  cli_async.events().OnBusData = [&evt_counter](Bus::FEvent data) { evt_counter++; };

  // check that callback fires with simple event with code
  Bus::FEvent evt_wait, evt_snd;
  FillEventData(&evt_wait, 1);
  evt_wait.Clone(&evt_snd);
  cli_async->WaitForEvent(std::move(evt_wait), zx::msec(0).to_nsecs(),
                          [&got_callback](bool result) {
                            EXPECT_TRUE(result);
                            got_callback = true;
                          });
  // just send whatever on the bus to guarantee that wait is already in effect
  cli_async->EnsurePublish(Bus::FEvent(), [&published]() { published = true; });
  WAIT_FOR_OK_AND_RESET(published);
  cli1->Publish(std::move(evt_snd));
  WAIT_FOR_OK_AND_RESET(got_callback);

  // check that callback fires on second event
  FillEventData(&evt_wait, 1);
  FillEventData(&evt_snd, 2);
  cli_async->WaitForEvent(std::move(evt_wait), zx::msec(0).to_nsecs(),
                          [&got_callback](bool result) {
                            EXPECT_TRUE(result);
                            got_callback = true;
                          });
  // just send whatever on the bus to guarantee that wait is already in effect
  cli_async->EnsurePublish(Bus::FEvent(), [&published]() { published = true; });
  WAIT_FOR_OK_AND_RESET(published);
  cli1->EnsurePublish(std::move(evt_snd));
  EXPECT_FALSE(got_callback);
  FillEventData(&evt_snd, 1);
  cli1->EnsurePublish(std::move(evt_snd));
  WAIT_FOR_OK_AND_RESET(got_callback);

  // check that event sent by same client doesn't trigger callback
  FillEventData(&evt_wait, 1);
  FillEventData(&evt_snd, 2);
  cli_async->WaitForEvent(std::move(evt_wait), zx::msec(15).to_nsecs(),
                          [&got_callback](bool result) {
                            EXPECT_FALSE(result);
                            got_callback = true;
                          });
  cli_async->EnsurePublish(std::move(evt_snd), [&published]() { published = true; });
  WAIT_FOR_OK_AND_RESET(published);
  WAIT_FOR_OK_AND_RESET(got_callback);

  EXPECT_EQ(evt_counter,
            3);  // check that all events actually made into the event callback
}

TEST_F(BusTest, EventWaitingEquality) {
  SyncManagerSync bm;
  GetSyncManager(bm.NewRequest());

  BusSync cli1;
  ASSERT_OK(bm->BusSubscribe(kMainTestBus, "c1", cli1.NewRequest()));

  BusAsync cli_async;
  ASSERT_OK(bm->BusSubscribe(kMainTestBus, "async", cli_async.NewRequest()));

  int evt_counter = 0;
  cli_async.events().OnBusData = [&evt_counter](Bus::FEvent data) { evt_counter++; };

  Bus::FEvent wait, publish;
  FillEventData(&wait, 1);
  FillEventData(&publish, 1, "msg", {1, 2, 3});
  TestEventWaiting(&cli1, &cli_async, true, std::move(wait), std::move(publish), "code match");

  FillEventData(&wait, 0, "msg");
  FillEventData(&publish, 1, "msg", {1, 2, 3});
  TestEventWaiting(&cli1, &cli_async, true, std::move(wait), std::move(publish), "message match");

  FillEventData(&wait, 0, "", {1, 2, 3});
  FillEventData(&publish, 1, "msg", {1, 2, 3});
  TestEventWaiting(&cli1, &cli_async, true, std::move(wait), std::move(publish), "arg match");

  FillEventData(&wait, 2);
  FillEventData(&publish, 1, "msg", {1, 2, 3});
  TestEventWaiting(&cli1, &cli_async, false, std::move(wait), std::move(publish), "code mismatch");

  FillEventData(&wait, 0, "bla");
  FillEventData(&publish, 1, "msg", {1, 2, 3});
  TestEventWaiting(&cli1, &cli_async, false, std::move(wait), std::move(publish),
                   "message mismatch");

  FillEventData(&wait, 0, "", {4, 5, 6});
  FillEventData(&publish, 1, "msg", {1, 2, 3});
  TestEventWaiting(&cli1, &cli_async, false, std::move(wait), std::move(publish), "arg mismatch");

  FillEventData(&wait, 1, "msg", {4, 5, 6});
  FillEventData(&publish, 1, "msg", {1, 2, 3});
  TestEventWaiting(&cli1, &cli_async, false, std::move(wait), std::move(publish),
                   "all match/arg mismatch");

  FillEventData(&wait, 1, "msg", {1, 2, 3});
  FillEventData(&publish, 1, "msg", {1, 2, 3});
  TestEventWaiting(&cli1, &cli_async, true, std::move(wait), std::move(publish), "all match");

  // check that all events actually made into the event callback
  ASSERT_TRUE(
      RunLoopWithTimeoutOrUntil([&evt_counter]() { return evt_counter == 8; }, zx::msec(100)));
}

TEST_F(BusTest, OnClientAttachedFiresForPreviousClients) {
  SyncManagerSync bm;
  GetSyncManager(bm.NewRequest());

  BusSync b1;
  ASSERT_OK(bm->BusSubscribe(kMainTestBus, "b1", b1.NewRequest()));
  BusSync b2;
  ASSERT_OK(bm->BusSubscribe(kMainTestBus, "b2", b2.NewRequest()));

  bool saw_b1 = false;
  bool saw_b2 = false;
  bool ok = false;
  BusAsync cli;
  cli.events().OnClientAttached = [&saw_b1, &saw_b2, &ok](fidl::StringPtr client) {
    if (client == "b1") {
      ASSERT_FALSE(saw_b1);
      saw_b1 = true;
    } else if (client == "b2") {
      ASSERT_FALSE(saw_b2);
      saw_b2 = true;
    } else {
      FAIL() << "Unexpected client " << client;
    }

    ok = saw_b1 && saw_b2;
  };
  ASSERT_OK(bm->BusSubscribe(kMainTestBus, "cli", cli.NewRequest()));
  WAIT_FOR_OK(ok);
}

}  // namespace testing
}  // namespace netemul
