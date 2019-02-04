// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/component/cpp/environment_services_helper.h>
#include <lib/component/cpp/testing/test_util.h>
#include <lib/component/cpp/testing/test_with_environment.h>

#include "sync_manager.h"

#define ASSERT_OK(st) ASSERT_EQ(ZX_OK, (st))
#define ASSERT_NOK(st) ASSERT_NE(ZX_OK, (st))

#define WAIT_FOR_OK(ok) \
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&ok]() { return ok; }, zx::sec(2)))
#define WAIT_FOR_OK_AND_RESET(ok) \
  WAIT_FOR_OK(ok);                \
  ok = false

namespace netemul {
namespace testing {

using component::testing::EnclosingEnvironment;
using component::testing::EnvironmentServices;
using component::testing::TestWithEnvironment;

static const char* kMainTestBus = "test-bus";
static const char* kAltTestBus = "alt-bus";

class BusTest : public TestWithEnvironment {
 public:
  using SyncManagerSync =
      fidl::SynchronousInterfacePtr<SyncManager::FSyncManager>;
  using BusSync = fidl::SynchronousInterfacePtr<Bus::FBus>;
  using BusAsync = fidl::InterfacePtr<Bus::FBus>;

 protected:
  void SetUp() override {
    fuchsia::sys::EnvironmentPtr parent_env;
    real_services()->ConnectToService(parent_env.NewRequest());

    svc_loop_ =
        std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);
    ASSERT_OK(svc_loop_->StartThread("testloop"));
    svc_ = std::make_unique<SyncManager>(svc_loop_->dispatcher());

    auto services =
        EnvironmentServices::Create(parent_env, svc_loop_->dispatcher());

    services->AddService(svc_->GetHandler());
    test_env_ = CreateNewEnclosingEnvironment("env", std::move(services));

    ASSERT_TRUE(WaitForEnclosingEnvToStart(test_env_.get()));
  }

  void TearDown() override {
    async::PostTask(svc_loop_->dispatcher(), [this]() {
      svc_.reset();
      svc_loop_->Quit();
    });
    svc_loop_->JoinThreads();
  }

  void GetSyncManager(
      fidl::InterfaceRequest<SyncManager::FSyncManager> manager) {
    test_env_->ConnectToService(std::move(manager));
  }

  void FillEventData(Bus::FEvent* event, int32_t code = 0,
                     const std::string& name = "",
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

  static bool EventEquals(const netemul::Bus::FEvent& e1,
                          const netemul::Bus::FEvent& e2) {
    return (e1.has_code() == e2.has_code() &&
            (!e1.has_code() || *e1.code() == *e2.code())) &&
           (e1.has_message() == e2.has_message() &&
            (!e1.has_message() || *e1.message() == *e2.message())) &&
           (e1.has_arguments() == e2.has_arguments() &&
            (!e1.has_arguments() ||
             (e1.arguments()->size() == e2.arguments()->size() &&
              memcmp(e1.arguments()->data(), e2.arguments()->data(),
                     e1.arguments()->size()) == 0)));
  }

  void DataExchangeTest(const Bus::FEvent& ref_event_1,
                        const Bus::FEvent& ref_event_2, bool ensure = false) {
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

  bool VectorSetEquals(const std::vector<std::string>& s1,
                       const std::vector<std::string>& s2) {
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
  cli1.events().OnBusData = [&received_data](Bus::FEvent event) {
    received_data = true;
  };

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

}  // namespace testing
}  // namespace netemul
