// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/component/cpp/environment_services_helper.h>
#include <lib/component/cpp/testing/test_util.h>
#include <lib/component/cpp/testing/test_with_environment.h>

#include "bus_manager.h"

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
  using BusManagerSync = fidl::SynchronousInterfacePtr<BusManager::FBusManager>;
  using BusSync = fidl::SynchronousInterfacePtr<Bus::FBus>;
  using BusAsync = fidl::InterfacePtr<Bus::FBus>;

 protected:
  void SetUp() override {
    fuchsia::sys::EnvironmentPtr parent_env;
    real_services()->ConnectToService(parent_env.NewRequest());

    svc_loop_ =
        std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);
    ASSERT_OK(svc_loop_->StartThread("testloop"));
    svc_ = std::make_unique<BusManager>(svc_loop_->dispatcher());

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

  void GetBusManager(fidl::InterfaceRequest<BusManager::FBusManager> manager) {
    test_env_->ConnectToService(std::move(manager));
  }

  void FillEventData(Bus::FEvent* event, int32_t code,
                     const std::string& name = "", size_t args_size = 0) {
    event->code = code;
    if (!name.empty()) {
      event->message = name;
    }
    event->arguments->clear();
    while (args_size > 0) {
      event->arguments->push_back(static_cast<uint8_t>(args_size--));
    }
  }

  static bool EventEquals(const Bus::FEvent& e1, const Bus::FEvent& e2) {
    return (e1.code == e2.code) && (e1.message == e2.message) &&
           (e2.arguments->size() == e1.arguments->size()) &&
           (memcmp(&e2.arguments.get()[0], &e1.arguments.get()[0],
                   e1.arguments->size()) == 0);
  }

  void DataExchangeTest(const Bus::FEvent& ref_event_1,
                        const Bus::FEvent& ref_event_2, bool ensure = false) {
    BusManagerSync bm;
    GetBusManager(bm.NewRequest());

    BusAsync cli1;
    ASSERT_OK(bm->Subscribe(kMainTestBus, "cli1", cli1.NewRequest()));
    ASSERT_TRUE(cli1.is_bound());

    BusAsync cli2;
    ASSERT_OK(bm->Subscribe(kMainTestBus, "cli2", cli2.NewRequest()));
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
    // these vectors are small enough that the O(n2) search here is not too
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
  std::unique_ptr<BusManager> svc_;
};

TEST_F(BusTest, CreateBusAndClient) {
  BusManagerSync bm;
  GetBusManager(bm.NewRequest());

  BusSync cli1;
  ASSERT_OK(bm->Subscribe(kMainTestBus, "cli1", cli1.NewRequest()));
  ASSERT_TRUE(cli1.is_bound());

  BusSync cli2;
  ASSERT_OK(bm->Subscribe(kMainTestBus, "cli2", cli2.NewRequest()));
  ASSERT_TRUE(cli2.is_bound());

  // client with name cli2 on same bus should be disallowed:
  BusSync cli3;
  ASSERT_OK(bm->Subscribe(kMainTestBus, "cli2", cli2.NewRequest()));
  RunLoopUntilIdle();
  ASSERT_FALSE(cli3.is_bound());
}

TEST_F(BusTest, ExchangeFullData) {
  Bus::FEvent ref_event_1, ref_event_2;
  FillEventData(&ref_event_1, 1, "Hello evt 1", 10);
  FillEventData(&ref_event_2, 2, "Hello evt 2", 20);

  DataExchangeTest(ref_event_1, ref_event_2);
}

TEST_F(BusTest, ExchangeFullDataEnsured) {
  Bus::FEvent ref_event_1, ref_event_2;
  FillEventData(&ref_event_1, 1, "Hello evt 1", 10);
  FillEventData(&ref_event_2, 2, "Hello evt 2", 20);

  DataExchangeTest(ref_event_1, ref_event_2, true);
}

TEST_F(BusTest, ExchangeCodeOnlyData) {
  Bus::FEvent ref_event_1, ref_event_2;
  FillEventData(&ref_event_1, 1);
  FillEventData(&ref_event_2, 2);

  DataExchangeTest(ref_event_1, ref_event_2);
}

TEST_F(BusTest, CrossTalk) {
  BusManagerSync bm;
  GetBusManager(bm.NewRequest());

  bool received_data = false;
  // attach a client to an alternate test bus
  BusAsync cli1;
  ASSERT_OK(bm->Subscribe(kAltTestBus, "cli1", cli1.NewRequest()));
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

TEST_F(BusTest, ClientObservation) {
  BusManagerSync bm;
  GetBusManager(bm.NewRequest());

  BusSync cli3;
  ASSERT_OK(bm->Subscribe(kMainTestBus, "c3", cli3.NewRequest()));
  std::vector<std::string> clients;
  ASSERT_OK(cli3->GetClients(&clients));
  ASSERT_TRUE(VectorSetEquals(clients, {"c3"}));

  BusAsync cli1;
  ASSERT_OK(bm->Subscribe(kMainTestBus, "c1", cli1.NewRequest()));
  ASSERT_TRUE(cli1.is_bound());

  bool ok = false;
  cli1.events().OnClientAttached = [&ok](fidl::StringPtr client) {
    ASSERT_EQ(client, "c2");
    ok = true;
  };
  cli1.events().OnClientDetached = [&ok](fidl::StringPtr client) {
    ASSERT_EQ(client, "c2");
    ok = true;
  };

  ASSERT_OK(cli3->GetClients(&clients));
  ASSERT_TRUE(VectorSetEquals(clients, {"c1", "c3"}));

  {
    BusAsync cli2;
    ASSERT_OK(bm->Subscribe(kMainTestBus, "c2", cli2.NewRequest()));
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

  // make sure to unbind cli1 first
  cli1.Unbind();
}

}  // namespace testing
}  // namespace netemul
