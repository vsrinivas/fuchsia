// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sandbox.h"
#include <lib/fit/sequencer.h>
#include <lib/fit/single_threaded_executor.h>
#include <lib/sys/cpp/termination_reason.h>
#include <iostream>
#include <unordered_set>
#include "lib/gtest/real_loop_fixture.h"

// A fairly large timeout is used to prevent flakiness in CQ, but we don't want
// to have a test that just blocks forever.
static const uint32_t kTimeoutSecs = 90;
static const char* kBusName = "test-bus";
static const char* kBusClientName = "sandbox_unittest";

namespace netemul {
namespace testing {

enum EventType {
  Event,
  OnClientAttached,
  OnClientDetached,
};

using namespace fuchsia::netemul;

class SandboxTest : public ::gtest::RealLoopFixture {
 protected:
  using TerminationReason = Sandbox::TerminationReason;

  void RunSandbox(bool expect_success, TerminationReason expect_reason) {
    Sandbox sandbox(std::move(sandbox_args_));
    bool done = false;
    int64_t o_exit_code;
    TerminationReason o_term_reason;

    sandbox.SetServicesCreatedCallback([this, &sandbox]() {
      if (connect_to_network_) {
        ConnectToNetwork(&sandbox);
      }
      if (collect_events_) {
        InstallEventCollection(&sandbox);
      }
    });

    sandbox.SetTerminationCallback([&done, &o_exit_code, &o_term_reason](
                                       int64_t exit_code,
                                       TerminationReason reason) {
      FXL_LOG(INFO) << "Sandbox terminated with (" << exit_code << ") reason: "
                    << sys::HumanReadableTerminationReason(reason);
      o_exit_code = exit_code;
      o_term_reason = reason;
      done = true;
    });

    sandbox.Start(dispatcher());

    ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&done]() { return done; },
                                          zx::sec(kTimeoutSecs)));

    // We quit the loop when sandbox terminates,
    // but because some of the tests will look at services in the sandbox when
    // we exit, we run the loop until idle to make sure the sandbox will have a
    // last chance to read any events pending.
    RunLoopUntilIdle();

    EXPECT_EQ(o_exit_code == 0, expect_success);
    EXPECT_EQ(o_term_reason, expect_reason);
  }

  void RunSandboxSuccess() { RunSandbox(true, TerminationReason::EXITED); }

  void RunSandboxInternalError() {
    RunSandbox(false, TerminationReason::INTERNAL_ERROR);
  }

  void RunSandboxFailure() { RunSandbox(false, TerminationReason::EXITED); }

  void SetCmx(const std::string& cmx) {
    ASSERT_TRUE(sandbox_args_.ParseFromString(cmx));
  }

  void EnableEventCollection() { collect_events_ = true; }
  void EnableNetworkService() { connect_to_network_ = true; }

  void CheckEvents(const std::vector<int32_t>& check) {
    for (const auto& v : check) {
      auto f = collected_codes_.find(v);
      EXPECT_FALSE(f == collected_codes_.end())
          << "Couldn't find event code " << v;
    }
  }

  bool PeekEvents(const std::unordered_set<int32_t>& check) {
    for (const auto& v : check) {
      auto f = collected_codes_.find(v);
      if (f == collected_codes_.end()) {
        return false;
      }
    }
    return true;
  }

  bool ObservedClient(const std::string& client) {
    return observed_clients_.find(client) != observed_clients_.end();
  }

  bool ClientDetached(const std::string& client) {
    return detached_clients_.find(client) != detached_clients_.end();
  }

  void SetOnEvent(fit::function<void(EventType)> on_event) {
    on_event_ = std::move(on_event);
  }

  const std::unordered_set<int32_t>& events() { return collected_codes_; }

  sync::BusPtr& bus() { return bus_; }

  network::NetworkManagerPtr& network_manager() { return net_manager_; }

  network::EndpointManagerPtr& endpoint_manager() { return endp_manager_; }

 private:
  void ConnectToNetwork(Sandbox* sandbox) {
    std::cout << "Connected to network" << std::endl;
    sandbox->sandbox_environment()->network_context().GetHandler()(
        net_ctx_.NewRequest());
    net_ctx_->GetNetworkManager(net_manager_.NewRequest());
    net_ctx_->GetEndpointManager(endp_manager_.NewRequest());
  }

  void InstallEventCollection(Sandbox* sandbox) {
    // connect to bus manager:
    sync::SyncManagerPtr syncManager;
    sandbox->sandbox_environment()->sync_manager().GetHandler()(
        syncManager.NewRequest());
    syncManager->BusSubscribe(kBusName, kBusClientName, bus_.NewRequest());
    bus_.events().OnBusData = [this](sync::Event event) {
      if (!event.has_code()) {
        return;
      }
      auto code = event.code();
      std::cout << "Observed event " << code << std::endl;
      // assert that code hasn't happened yet.
      // given we're putting codes in a set, it's an invalid test setup
      // to have child procs publish the same code multiple times
      ASSERT_TRUE(collected_codes_.find(code) == collected_codes_.end());
      collected_codes_.insert(code);
      if (on_event_) {
        on_event_(EventType::Event);
      }
    };
    bus_.events().OnClientAttached = [this](fidl::StringPtr client) {
      // Ensure no two clients with the same name get attached to bus,
      // doing so may result in flaky tests due to timing
      // this is here mostly to catch bad test setups
      std::cout << "Observed client " << client << std::endl;
      ASSERT_TRUE(observed_clients_.find(client) == observed_clients_.end());
      observed_clients_.insert(client);
      if (on_event_) {
        on_event_(EventType::OnClientAttached);
      }
    };
    bus_.events().OnClientDetached = [this](fidl::StringPtr client) {
      // just keep a record of detached clients
      detached_clients_.insert(client);
      if (on_event_) {
        on_event_(EventType::OnClientDetached);
      }
    };
  }

  fit::function<void(EventType)> on_event_;
  bool collect_events_ = false;
  bool connect_to_network_ = false;
  SandboxArgs sandbox_args_;
  std::unordered_set<int32_t> collected_codes_;
  std::unordered_set<std::string> observed_clients_;
  std::unordered_set<std::string> detached_clients_;
  sync::BusPtr bus_;
  network::NetworkContextPtr net_ctx_;
  network::NetworkManagerPtr net_manager_;
  network::EndpointManagerPtr endp_manager_;
};

TEST_F(SandboxTest, SimpleSuccess) {
  SetCmx(R"(
{
   "environment" : {
      "test" : [ "fuchsia-pkg://fuchsia.com/netemul_sandbox_test#meta/dummy_proc.cmx" ]
   }
})");
  RunSandboxSuccess();
}

TEST_F(SandboxTest, MalformedFacet) {
  SandboxArgs args;
  ASSERT_FALSE(args.ParseFromString(R"( {bad, json} )"));
}

TEST_F(SandboxTest, SimpleFailure) {
  SetCmx(R"(
{
   "default_url": "fuchsia-pkg://fuchsia.com/netemul_sandbox_test#meta/dummy_proc.cmx",
   "environment" : {
      "test" : [ { "arguments": ["-f"] } ]
   }
}
)");
  RunSandboxFailure();
}

TEST_F(SandboxTest, ConfirmOnBus) {
  SetCmx(R"(
{
   "default_url": "fuchsia-pkg://fuchsia.com/netemul_sandbox_test#meta/dummy_proc.cmx",
   "environment" : {
      "test" : [ { "arguments": ["-p", "3"] } ]
   }
}
)");
  EnableEventCollection();
  RunSandboxSuccess();
  CheckEvents({3});
}

TEST_F(SandboxTest, FastChildren) {
  // Make root test wait so children exits first
  SetCmx(R"(
  {
    "default_url": "fuchsia-pkg://fuchsia.com/netemul_sandbox_test#meta/dummy_proc.cmx",
    "environment" : {
      "name" : "root",
      "test" : [ { "arguments": ["-p", "1", "-w", "30"] } ],
      "children" : [
        {
          "name" : "child",
          "test" : [{
            "arguments" : ["-p", "2", "-n", "child"]
          }]
        }
      ]
    }
  }
  )");
  EnableEventCollection();
  RunSandboxSuccess();
  CheckEvents({1, 2});
}

TEST_F(SandboxTest, FastRoot) {
  // Make child test wait so root exits first
  SetCmx(R"(
  {
    "default_url": "fuchsia-pkg://fuchsia.com/netemul_sandbox_test#meta/dummy_proc.cmx",
    "environment" : {
      "name" : "root",
      "test" : [ { "arguments": ["-p", "1"] } ],
      "children" : [
        {
          "name" : "child",
          "test" : [{
            "arguments" : ["-p", "2", "-n", "child", "-w", "30"]
          }]
        }
      ]
    }
  }
  )");
  EnableEventCollection();
  RunSandboxSuccess();
  CheckEvents({1, 2});
}

TEST_F(SandboxTest, FailedSetupCausesFailure) {
  SetCmx(R"(
  {
    "default_url": "fuchsia-pkg://fuchsia.com/netemul_sandbox_test#meta/dummy_proc.cmx",
    "environment" : {
      "test" : [ { "arguments": ["-p", "1"] } ],
      "setup" : [{
        "arguments" : ["-f"]
      }]
    }
  }
  )");
  EnableEventCollection();
  RunSandboxInternalError();
  // root proc should not have run, so events should be empty
  EXPECT_TRUE(events().empty());
}

TEST_F(SandboxTest, AppsAreLaunched) {
  // Launch root waiting for event 100, responds with event 4
  // Launch 3 apps and observe that they ran
  // then Signal root with event 100
  SetCmx(R"(
  {
    "default_url": "fuchsia-pkg://fuchsia.com/netemul_sandbox_test#meta/dummy_proc.cmx",
    "environment" : {
      "test" : [ { "arguments": ["-e", "100", "-p", "4"] } ],
      "apps" : [
        {
          "arguments" : ["-n", "app1", "-p", "1"]
        },
        {
          "arguments" : ["-n", "app2", "-p", "2"]
        },
        {
          "arguments" : ["-n", "app3", "-p", "3"]
        }
      ]
    }
  }
  )");
  SetOnEvent([this](EventType type) {
    if (type == EventType::OnClientDetached) {
      return;
    }
    // if all app events are seen and root is waiting for us
    // unlock root with event code 100
    if (PeekEvents({1, 2, 3}) && ObservedClient("root")) {
      sync::Event evt;
      evt.set_code(100);
      bus()->Publish(std::move(evt));
    }
  });
  EnableEventCollection();
  RunSandboxSuccess();
  // all events must be there at the end
  CheckEvents({1, 2, 3, 4});
}

TEST_F(SandboxTest, AppExitCodesAreIgnored) {
  // Launch root waiting for event 100, responds with event 2
  // Launch app that published event 1 and will fail
  // sandbox should ignore "app" exit codes
  SetCmx(R"(
  {
    "default_url": "fuchsia-pkg://fuchsia.com/netemul_sandbox_test#meta/dummy_proc.cmx",
    "environment" : {
      "test" : [ { "arguments": ["-e", "100", "-p", "2"] } ],
      "apps" : [
        {
          "arguments" : ["-n", "app1", "-p", "1", "-f"]
        }
      ]
    }
  }
  )");
  SetOnEvent([this](EventType type) {
    if (type == EventType::OnClientDetached) {
      return;
    }
    // if all app events are seen and root is waiting for us
    // unlock root with event code 100
    if (PeekEvents({1}) && ObservedClient("root")) {
      sync::Event evt;
      evt.set_code(100);
      bus()->Publish(std::move(evt));
    }
  });
  EnableEventCollection();
  RunSandboxSuccess();
  // all events must be there at the end
  CheckEvents({1, 2});
}

TEST_F(SandboxTest, SetupProcsAreOperatedSequentially) {
  SetCmx(R"(
  {
    "default_url": "fuchsia-pkg://fuchsia.com/netemul_sandbox_test#meta/dummy_proc.cmx",
    "environment" : {
      "test" : [ { "arguments": ["-p", "4"] } ],
      "setup" : [
        {
          "arguments" : ["-p", "1", "-n", "setup1", "-w", "10"]
        },
        {
          "arguments" : ["-p", "2", "-n", "setup2", "-w", "5"]
        },
        {
          "arguments" : ["-p", "3", "-n", "setup3"]
        }
      ]
    }
  }
  )");
  int counter = 0;
  SetOnEvent([this, &counter](EventType type) {
    if (type != EventType::Event) {
      return;
    }
    counter++;
    switch (counter) {
      case 1:
        EXPECT_TRUE(ObservedClient("setup1"));
        CheckEvents({1});
        break;
      case 2:
        EXPECT_TRUE(ObservedClient("setup2"));
        EXPECT_TRUE(ClientDetached("setup1"));
        CheckEvents({1, 2});
        break;
      case 3:
        EXPECT_TRUE(ObservedClient("setup3"));
        EXPECT_TRUE(ClientDetached("setup2"));
        CheckEvents({1, 2, 3});
        break;
      case 4:
        EXPECT_TRUE(ObservedClient("root"));
        EXPECT_TRUE(ClientDetached("setup3"));
        CheckEvents({1, 2, 3});
        break;
      default:
        FAIL() << "counter should not have this value";
    }
  });
  EnableEventCollection();
  RunSandboxSuccess();
  CheckEvents({1, 2, 3, 4});
}

TEST_F(SandboxTest, SetupRunsBeforeTest) {
  SetCmx(R"(
  {
    "default_url": "fuchsia-pkg://fuchsia.com/netemul_sandbox_test#meta/dummy_proc.cmx",
    "environment" : {
      "setup" : [
        {"arguments" : ["-p", "1", "-n", "setup1", "-w", "2"]}
      ],
      "test" : [
        {"arguments" : ["-p", "3", "-n", "test1"]},
        {"arguments" : ["-p", "2"]}
      ]
    }
  }
  )");
  int counter = 0;
  SetOnEvent([this, &counter](EventType type) {
    if (type != EventType::Event) {
      return;
    }
    counter++;
    switch (counter) {
      case 1:
        EXPECT_TRUE(ObservedClient("setup1"));
        CheckEvents({1});
        EXPECT_FALSE(ObservedClient("test1"));
        EXPECT_FALSE(ObservedClient("root"));
        break;
      default:
        EXPECT_TRUE(ClientDetached("setup1"));
        break;
    }
  });
  EnableEventCollection();
  RunSandboxSuccess();
  CheckEvents({1, 2, 3});
}

TEST_F(SandboxTest, DuplicateNetworkNameFails) {
  SetCmx(R"(
  {
    "networks" : [
      {
        "name" : "net"
      },
      {
        "name" : "net"
      }
    ]
  }
  )");
  RunSandboxInternalError();
}

TEST_F(SandboxTest, DuplicateEndpointNameFails) {
  SetCmx(R"(
  {
    "networks" : [
      {
        "name" : "net1",
        "endpoints" : [{
          "name" : "ep"
        }]
      },
      {
        "name" : "net2",
        "endpoints" : [{
          "name" : "ep"
        }]
      }
    ]
  }
  )");
  RunSandboxInternalError();
}

TEST_F(SandboxTest, ValidNetworkSetup) {
  // - Configures 2 networks with 2 endpoints each
  // - waits for root process to start and then
  //   connects to network FIDL service to check
  //   that the networks and endpoints were
  //   created correctly
  // - finally, tries to attach endpoints to network again
  //   to asses that they were correctly put in place
  SetCmx(R"(
  {
    "default_url": "fuchsia-pkg://fuchsia.com/netemul_sandbox_test#meta/dummy_proc.cmx",
    "environment" : {
       "test" : [ { "arguments": ["-e", "100", "-p", "1"] } ]
    },
    "networks" : [
      {
        "name" : "net1",
        "endpoints" : [
          { "name" : "ep1" },
          { "name" : "ep2" }
        ]
     },
     {
       "name" : "net2",
       "endpoints" : [
         { "name" : "ep3" },
         { "name" : "ep4" }
       ]
     }
    ]
  }
  )");
  EnableNetworkService();
  EnableEventCollection();
  std::vector<std::string> networks({"net1", "net2"});
  std::vector<std::string> endpoints({"ep1", "ep2", "ep3", "ep4"});
  std::vector<std::pair<int, std::string>> attachments({
      std::make_pair<int, std::string>(0, "ep1"),
      std::make_pair<int, std::string>(0, "ep2"),
      std::make_pair<int, std::string>(1, "ep3"),
      std::make_pair<int, std::string>(1, "ep4"),
  });
  std::vector<network::NetworkPtr> found_nets;

  auto nets = networks.begin();
  auto eps = endpoints.begin();
  auto attach = attachments.begin();

  fit::function<void()> check;
  // check will keep a reference to itself so
  // it can recur.
  // Plus, it'll keep a reference to the iterators
  // so it runs all the checks over network, endpoint, and attachment
  check = [&nets, &eps, &networks, &endpoints, &attach, &attachments, &check,
           &found_nets, this]() {
    if (nets != networks.end()) {
      // iterate over networks and check they're there
      auto lookup = *nets++;
      std::cout << "checking network " << lookup << std::endl;
      network_manager()->GetNetwork(
          lookup,
          [&check, &found_nets](fidl::InterfaceHandle<network::Network> net) {
            ASSERT_TRUE(net.is_valid());
            // keep network for attachments check
            found_nets.emplace_back(net.Bind());
            check();
          });
    } else if (eps != endpoints.end()) {
      // iterate over endpoints and check they're there
      auto lookup = *eps++;
      std::cout << "checking endpoint " << lookup << std::endl;
      endpoint_manager()->GetEndpoint(
          lookup, [&check](fidl::InterfaceHandle<network::Endpoint> ep) {
            ASSERT_TRUE(ep.is_valid());
            check();
          });
    } else if (attach != attachments.end()) {
      // iterate over attachments and check they're there
      auto& a = *attach++;
      std::cout << "checking endpoint " << a.second << " is in network"
                << std::endl;
      found_nets[a.first]->AttachEndpoint(
          a.second, [&check](zx_status_t status) {
            ASSERT_EQ(status, ZX_ERR_ALREADY_EXISTS);
            check();
          });
    } else {
      sync::Event event;
      event.set_code(100);
      bus()->Publish(std::move(event));
    }
  };

  // when we get the client attached event for root,
  // we call the check closure to run the tests.
  // at the end of the check closure, it'll
  // signal the root test with event code 100
  // and finish the test.
  SetOnEvent([this, &check](EventType type) {
    if (type != EventType::OnClientAttached) {
      return;
    }
    check();
  });
  RunSandboxSuccess();
  CheckEvents({1});
}

TEST_F(SandboxTest, ManyTests) {
  std::stringstream ss;
  ss << R"({ "default_url" : "fuchsia-pkg://fuchsia.com/netemul_sandbox_test#meta/dummy_proc.cmx",
             "environment" : { "test" : [)";
  const int test_count = 10;
  std::vector<int32_t> expect;
  expect.reserve(test_count);
  for (int i = 0; i < test_count; i++) {
    if (i != 0) {
      ss << ",";
    }
    ss << R"({"arguments":["-p",")" << i << R"(", "-n", "t)" << i << R"("]})";
    expect.push_back(i);
  }
  ss << "]}}";
  SetCmx(ss.str());
  EnableEventCollection();
  RunSandboxSuccess();
  CheckEvents(expect);
}

TEST_F(SandboxTest, NoTestsIsFailedtest) {
  // Even if we run setup stuff,
  // if no |tests| are defined in any environments, we consider it a failure.
  SetCmx(R"(
  {
    "default_url": "fuchsia-pkg://fuchsia.com/netemul_sandbox_test#meta/dummy_proc.cmx",
    "environment" : {
      "setup" : [
        {"arguments" : ["-n", "setup1"]}
      ],
      "test" : []
    }
  }
  )");
  RunSandboxInternalError();
}

TEST_F(SandboxTest, DisabledTestSucceeds) {
  // Start with a component that is instructed to fail,
  // but mark the test as disabled.
  // expect sandbox to exit with success.
  SetCmx(R"(
{
   "disabled" : true,
   "default_url": "fuchsia-pkg://fuchsia.com/netemul_sandbox_test#meta/dummy_proc.cmx",
   "environment" : {
      "test" : [ { "arguments": ["-f"] } ]
   }
}
)");
  RunSandboxSuccess();
}

TEST_F(SandboxTest, NonexistentPackageUrl) {
  SetCmx(R"(
{
   "environment" : {
      "test" : ["fuchsia-pkg://fuchsia.com/netemul_nonexistent_test#meta/something.cmx"]
   }
}
)");
  RunSandbox(false, TerminationReason::PACKAGE_NOT_FOUND);
}

TEST_F(SandboxTest, TimeoutFires) {
  SetCmx(R"(
{
   "default_url": "fuchsia-pkg://fuchsia.com/netemul_sandbox_test#meta/dummy_proc.cmx",
   "timeout" : 1,
   "environment" : {
      "test" : [ { "arguments": ["-w", "10000"] } ]
   }
}
)");
  // expect that we'll fail due to the timeout of 1s < 10s of wait in the dummy
  // proc:
  RunSandbox(false, TerminationReason::EXITED);
}

TEST_F(SandboxTest, ProcessSucceedsBeforeTimeoutFires) {
  SetCmx(R"(
{
   "timeout" : 5,
   "environment" : {
      "test" : [ "fuchsia-pkg://fuchsia.com/netemul_sandbox_test#meta/dummy_proc.cmx" ]
   }
}
)");
  // if a test succeeds, even though we have a timeout, we should succeed
  // normally:
  RunSandboxSuccess();
}

TEST_F(SandboxTest, Sequencer) {
  fit::sequencer seq;
  fit::single_threaded_executor exec;
  std::stringstream ss;

  std::vector<fit::promise<>> proms;
  proms.emplace_back(seq.wrap(fit::make_promise([&]() { ss << "a"; })));

  proms.emplace_back(seq.wrap(fit::make_promise([&]() { ss << "b"; })));

  exec.schedule_task(fit::join_promise_vector(std::move(proms)));
  exec.run();

  EXPECT_EQ(ss.str(), "ab");
}

}  // namespace testing
}  // namespace netemul
