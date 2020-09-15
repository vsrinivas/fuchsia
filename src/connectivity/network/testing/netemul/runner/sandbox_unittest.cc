// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sandbox.h"

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/fit/sequencer.h>
#include <lib/fit/single_threaded_executor.h>
#include <lib/sys/cpp/termination_reason.h>

#include <iostream>
#include <unordered_set>

#include "lib/gtest/real_loop_fixture.h"
#include "log_listener_test_helpers.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/testing/predicates/status.h"

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

  void RunSandbox(SandboxResult::Status expect) {
    Sandbox sandbox(std::move(sandbox_args_));
    bool done = false;
    SandboxResult o_result;

    sandbox.SetRootEnvironmentCreatedCallback([this](ManagedEnvironment* env) {
      if (log_event_) {
        fidl::InterfaceHandle<fuchsia::logger::LogListenerSafe> listener;
        log_listener_ = std::make_unique<TestListener>(listener.NewRequest());
        log_listener_->SetObserver(
            [this](const fuchsia::logger::LogMessage& msg) { log_event_(msg); });
        fidl::InterfacePtr<fuchsia::logger::Log> log;
        env->ConnectToService(fuchsia::logger::Log::Name_, log.NewRequest().TakeChannel());
        log->ListenSafe(std::move(listener), nullptr);
      }
    });

    sandbox.SetServicesCreatedCallback([this, &sandbox]() {
      if (connect_to_network_) {
        ConnectToNetwork(&sandbox);
      }
      if (collect_events_) {
        InstallEventCollection(&sandbox);
      }
    });

    sandbox.SetTerminationCallback([&done, &o_result](SandboxResult result) {
      FX_LOGS(INFO) << "Sandbox terminated with status: " << result;
      o_result = std::move(result);
      done = true;
    });

    sandbox.Start(dispatcher());

    RunLoopUntil([&done]() { return done; });

    // We quit the loop when sandbox terminates,
    // but because some of the tests will look at services in the sandbox when
    // we exit, we run the loop until idle to make sure the sandbox will have a
    // last chance to read any events pending.
    RunLoopUntilIdle();

    // If we're expecting unspecified status, we will just check for expected
    // failure. That's for failure cases that can have races on different
    // failure points.
    if (expect == SandboxResult::Status::UNSPECIFIED) {
      EXPECT_FALSE(o_result.is_success());
    } else {
      EXPECT_EQ(o_result.status(), expect);
    }
  }

  void RunSandboxSuccess() { RunSandbox(SandboxResult::SUCCESS); }

  void RunSandboxFailure() { RunSandbox(SandboxResult::TEST_FAILED); }

  void SetCmx(const std::string& cmx, bool disable_logging = true) {
    ASSERT_TRUE(sandbox_args_.ParseFromString(cmx));
    if (disable_logging) {
      // disable all syslog logging for unit tests.
      sandbox_args_.config.environment().DisableLogging(true);
    }
  }

  void EnableEventCollection() { collect_events_ = true; }
  void EnableNetworkService() { connect_to_network_ = true; }
  void EnableLogCapture(fit::function<void(const fuchsia::logger::LogMessage&)> callback) {
    log_event_ = std::move(callback);
  }

  void CheckEvents(const std::vector<int32_t>& check) {
    for (const auto& v : check) {
      auto f = collected_codes_.find(v);
      EXPECT_FALSE(f == collected_codes_.end()) << "Couldn't find event code " << v;
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

  void SetOnEvent(fit::function<void(EventType)> on_event) { on_event_ = std::move(on_event); }

  const std::unordered_set<int32_t>& events() { return collected_codes_; }

  sync::BusPtr& bus() { return bus_; }

  network::NetworkManagerPtr& network_manager() { return net_manager_; }

  network::EndpointManagerPtr& endpoint_manager() { return endp_manager_; }

  SandboxArgs TakeArgs() { return std::move(sandbox_args_); }

 private:
  void ConnectToNetwork(Sandbox* sandbox) {
    std::cout << "Connected to network" << std::endl;
    sandbox->sandbox_environment()->network_context().GetHandler()(net_ctx_.NewRequest());
    net_ctx_->GetNetworkManager(net_manager_.NewRequest());
    net_ctx_->GetEndpointManager(endp_manager_.NewRequest());
  }

  void InstallEventCollection(Sandbox* sandbox) {
    // connect to bus manager:
    sync::SyncManagerPtr syncManager;
    sandbox->sandbox_environment()->sync_manager().GetHandler()(syncManager.NewRequest());
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
      ASSERT_TRUE(client.has_value());
      ASSERT_TRUE(observed_clients_.find(client.value()) == observed_clients_.end());
      observed_clients_.insert(client.value());
      if (on_event_) {
        on_event_(EventType::OnClientAttached);
      }
    };
    bus_.events().OnClientDetached = [this](fidl::StringPtr client) {
      // just keep a record of detached clients
      ASSERT_TRUE(client.has_value());
      detached_clients_.insert(client.value());
      if (on_event_) {
        on_event_(EventType::OnClientDetached);
      }
    };
  }

  fit::function<void(EventType)> on_event_;
  bool collect_events_ = false;
  bool connect_to_network_ = false;
  fit::function<void(const fuchsia::logger::LogMessage&)> log_event_;
  SandboxArgs sandbox_args_;
  std::unordered_set<int32_t> collected_codes_;
  std::unordered_set<std::string> observed_clients_;
  std::unordered_set<std::string> detached_clients_;
  std::unique_ptr<TestListener> log_listener_;
  sync::BusPtr bus_;
  network::NetworkContextPtr net_ctx_;
  network::NetworkManagerPtr net_manager_;
  network::EndpointManagerPtr endp_manager_;
};

TEST_F(SandboxTest, SimpleSuccess) {
  SetCmx(R"(
{
   "environment" : {
      "test" : [ "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx" ]
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
   "default_url": "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx",
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
   "default_url": "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx",
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
    "default_url": "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx",
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
    "default_url": "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx",
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
    "default_url": "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx",
    "environment" : {
      "test" : [ { "arguments": ["-p", "1"] } ],
      "setup" : [{
        "arguments" : ["-f"]
      }]
    }
  }
  )");
  EnableEventCollection();
  RunSandbox(SandboxResult::Status::SETUP_FAILED);
  // root proc should not have run, so events should be empty
  EXPECT_TRUE(events().empty());
}

TEST_F(SandboxTest, AppsAreLaunched) {
  // Launch root waiting for event 100, responds with event 4
  // Launch 3 apps and observe that they ran
  // then Signal root with event 100
  SetCmx(R"(
  {
    "default_url": "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx",
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
    "default_url": "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx",
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
    "default_url": "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx",
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
    "default_url": "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx",
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

TEST_F(SandboxTest, SetupRunsBeforeTestOnChildren) {
  // Tests that empty root environments with children that requires
  // setup and tests will succeed. This is basically the same test setup
  // as "SetupRunsBeforeTest" but with an empty root environment.
  SetCmx(R"(
  {
    "default_url": "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx",
    "environment" : {
      "children": [{
        "setup" : [
          {"arguments" : ["-p", "1", "-n", "setup1", "-w", "2"]}
        ],
        "test" : [
          {"arguments" : ["-p", "3", "-n", "test1"]},
          {"arguments" : ["-p", "2"]}
        ]
      }]
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
  RunSandbox(SandboxResult::Status::NETWORK_CONFIG_FAILED);
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
  RunSandbox(SandboxResult::Status::NETWORK_CONFIG_FAILED);
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
    "default_url": "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx",
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
  check = [&nets, &eps, &networks, &endpoints, &attach, &attachments, &check, &found_nets, this]() {
    if (nets != networks.end()) {
      // iterate over networks and check they're there
      auto lookup = *nets++;
      std::cout << "checking network " << lookup << std::endl;
      network_manager()->GetNetwork(
          lookup, [&check, &found_nets](fidl::InterfaceHandle<network::Network> net) {
            ASSERT_TRUE(net.is_valid());
            // keep network for attachments check
            found_nets.emplace_back(net.Bind());
            check();
          });
    } else if (eps != endpoints.end()) {
      // iterate over endpoints and check they're there
      auto lookup = *eps++;
      std::cout << "checking endpoint " << lookup << std::endl;
      endpoint_manager()->GetEndpoint(lookup,
                                      [&check](fidl::InterfaceHandle<network::Endpoint> ep) {
                                        ASSERT_TRUE(ep.is_valid());
                                        check();
                                      });
    } else if (attach != attachments.end()) {
      // iterate over attachments and check they're there
      auto& a = *attach++;
      std::cout << "checking endpoint " << a.second << " is in network" << std::endl;
      found_nets[a.first]->AttachEndpoint(a.second, [&check](zx_status_t status) {
        ASSERT_STATUS(status, ZX_ERR_ALREADY_BOUND);
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
  SetOnEvent([&check](EventType type) {
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
  ss << R"({ "default_url" : "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx",
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
    "default_url": "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx",
    "environment" : {
      "setup" : [
        {"arguments" : ["-n", "setup1"]}
      ],
      "test" : []
    }
  }
  )");
  RunSandbox(SandboxResult::Status::EMPTY_TEST_SET);
}

TEST_F(SandboxTest, DisabledTestSucceeds) {
  // Start with a component that is instructed to fail,
  // but mark the test as disabled.
  // expect sandbox to exit with success.
  SetCmx(R"(
{
   "disabled" : true,
   "default_url": "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx",
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
  RunSandbox(SandboxResult::Status::COMPONENT_FAILURE);
}

TEST_F(SandboxTest, TimeoutFires) {
  SetCmx(R"(
{
   "default_url": "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx",
   "timeout" : 1,
   "environment" : {
      "test" : [ { "arguments": ["-w", "10000"] } ]
   }
}
)");
  // expect that we'll fail due to the timeout of 1s < 10s of wait in the dummy
  // proc:
  RunSandbox(SandboxResult::Status::TIMEOUT);
}

TEST_F(SandboxTest, ProcessSucceedsBeforeTimeoutFires) {
  SetCmx(R"(
{
   "timeout" : 60,
   "environment" : {
      "test" : [ "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx" ]
   }
}
)");
  // if a test succeeds, even though we have a timeout, we should succeed
  // normally. We're using a large timeout here to prevent stalls in
  // CQ bots to cause a false negative.
  RunSandboxSuccess();
}

TEST_F(SandboxTest, BadServiceCausesFailure) {
  SetCmx(R"(
{
   "environment" : {
     "services": {
        "fuchsia.dummy.service" : "fuchsia-pkg://fuchsia.com/bad_package#meta/bad_service.cmx"
      },
      "test": [{
          "url" : "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx",
          "arguments" : ["-w", "5000", "-s", "fuchsia.dummy.service"]
      }]
   }
}
)");
  RunSandbox(SandboxResult::Status::SERVICE_EXITED);
}

TEST_F(SandboxTest, ServiceExittingCausesFailure) {
  SetCmx(R"(
{
   "environment" : {
     "services": {
        "fuchsia.dummy.service" : {
           "url": "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx",
           "arguments" : ["-f"]
        }
      },
      "test": [{
          "url" : "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx",
          "arguments" : ["-w", "5000", "-s", "fuchsia.dummy.service"]
      }]
   }
}
)");
  RunSandbox(SandboxResult::Status::SERVICE_EXITED);
}

TEST_F(SandboxTest, DestructorRunsCleanly) {
  // This test verifies that if the sandbox is destroyed while tests are
  // running inside it, it'll shutdown cleanly.
  // Dummy proc is launched with a large wait value in -w to ensure that we destroy the sandbox
  // while it is still running.
  SetCmx(R"(
{
   "default_url": "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx",
   "environment" : {
      "test" : [ { "arguments": ["-w", "90000"] } ]
   }
}
)");
  auto sandbox = std::make_unique<Sandbox>(TakeArgs());
  sandbox->SetTerminationCallback([](SandboxResult result) { FAIL() << "Shouldn't exit"; });
  sandbox->Start(dispatcher());
  // Give enough time for the process to actually open the file:
  RunLoopWithTimeout(zx::msec(15));
  // force the descrutor to run:
  sandbox = nullptr;
}

TEST_F(SandboxTest, EnvironmentsWithSameNameFail) {
  SetCmx(R"(
{
   "environment" : {
      "children" : [
       {
          "name" : "my-env",
          "test" : [ "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx" ]
       },
       {
          "name" : "my-env",
          "test" : [ "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx" ]
       }
      ]
   }
})");
  // Failures for environment with same name can come
  // from different sources and is racy, to prevent
  // test flakiness we just check that it'll fail cleanly,
  // and not expect a specific return code.
  RunSandbox(SandboxResult::Status::UNSPECIFIED);
}

constexpr char expected_tag[] = "dummy_proc";

TEST_F(SandboxTest, SyslogWithNoKlog) {
  SetCmx(R"(
{
   "environment" : {
      "test" : [{
         "url": "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx",
         "arguments" : ["-l", "hello", "-e", "100"]
      }],
      "logger_options": {
         "enabled": false,
         "klogs_enabled": false
      }
   }
})",
         false);
  EnableEventCollection();
  EnableLogCapture([this](const fuchsia::logger::LogMessage& msg) {
    if (std::find(msg.tags.begin(), msg.tags.end(), expected_tag) != msg.tags.end()) {
      FX_LOGS(INFO) << "Got log tagged with '" << expected_tag << "'! Sending event...";
      sync::Event evt;
      evt.set_code(100);
      bus()->Publish(std::move(evt));
      FX_LOGS(INFO) << "Published event!";
    } else {
      for (const auto& tag : msg.tags) {
        FX_LOGS(INFO) << "Got non-matching tag " << tag << "; waiting...";
      }
    }
  });
  RunSandboxSuccess();
}

TEST_F(SandboxTest, SyslogWithKlog) {
  SetCmx(R"(
{
   "environment" : {
      "test" : [{
         "url": "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx",
         "arguments" : ["-l", "hello", "-e", "100"]
      }],
      "logger_options": {
         "enabled": false,
         "klogs_enabled": true
      }
   }
})",
         false);
  EnableEventCollection();
  int klog_count = 0;
  bool got_dummy_log = false;
  EnableLogCapture([this, &klog_count, &got_dummy_log](const fuchsia::logger::LogMessage& msg) {
    if (std::find(msg.tags.begin(), msg.tags.end(), expected_tag) != msg.tags.end()) {
      got_dummy_log = true;
    } else if (std::find(msg.tags.begin(), msg.tags.end(), "klog") != msg.tags.end()) {
      klog_count++;
    } else {
      for (const auto& tag : msg.tags) {
        FX_LOGS(INFO) << "Got non-matching tag " << tag << "; waiting...";
      }
    }
    if (got_dummy_log && klog_count != 0) {
      sync::Event evt;
      evt.set_code(100);
      bus()->Publish(std::move(evt));
    }
  });
  RunSandboxSuccess();
  ASSERT_NE(klog_count, 0);
}

TEST_F(SandboxTest, InvalidEnvironmentDevice) {
  // Check that sandbox will fail with environment configuration failure
  // if environment.devices contains an endpoint name that doesn't exist:
  SetCmx(R"(
{
   "environment" : {
      "test" : [ "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx" ],
      "devices": ["bad-ep"]
   },
   "networks": [{
       "name": "net",
       "endpoints": [{"name":"ep"}]
   }]
})");
  RunSandbox(SandboxResult::Status::ENVIRONMENT_CONFIG_FAILED);
}

TEST_F(SandboxTest, ServicesHaveVdev) {
  // Check that /vdev is passed to services that have "dev" in their sandbox.
  // We run one dummy_proc as a service fuchsia.dummy.service that will check for a path
  // "/dev/class/ethernet/ep" and then publish event 1.
  // The test will hit "fuchsia.dummy.service" (to make the service proc launch) and then
  // publish event 1.
  // Then we can check that the test suite runs to completion. (If /vdev was not
  // available, we'd fail due to the dummy service "crashing")
  SetCmx(R"(
{
   "default_url": "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx",
   "environment": {
      "test" : [{"arguments":["-s", "fuchsia.dummy.service", "-e", "1", "-n", "test"]}],
      "services" : {
        "fuchsia.dummy.service": {
            "url": "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc-with-dev.cmx",
            "arguments": ["-P", "/vdev/class/ethernet/ep", "-p", "1", "-n", "svc"]
        }
      },
      "devices": ["ep"]
   },
   "networks": [{
       "name": "net",
       "endpoints": [{"name":"ep"}]
   }]
}
)");
  RunSandboxSuccess();
}

TEST_F(SandboxTest, NotAllServicesHaveVdev) {
  // This is the same test as ServicesHaveVdev, but it'll fail because we're launching a version of
  // dummy_proc that does not have "dev" in its sandbox. This asserts that we're using the "dev"
  // field in the service's component manifest to decide whether it receives /vdev in its namespace
  SetCmx(R"(
{
   "default_url": "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc.cmx",
   "environment": {
      "test" : [{"arguments":["-s", "fuchsia.dummy.service", "-e", "1", "-n", "test"]}],
      "services" : {
        "fuchsia.dummy.service": {"arguments": ["-P", "/vdev/class/ethernet/ep", "-p", "1", "-n", "svc"]}
      },
      "devices": ["ep"]
   },
   "networks": [{
       "name": "net",
       "endpoints": [{"name":"ep"}]
   }]
}
)");
  // dummy service will just exit with an error because it won't be able to find /vdev:
  RunSandbox(SandboxResult::Status::SERVICE_EXITED);
}

TEST_F(SandboxTest, ExposesNetworkDevice) {
  // Create an endpoint with NETWORK_DEVICE backing and stat its file from the dummy_proc to see
  // that it got mounted in /vdev
  SetCmx(R"(
{
   "default_url": "fuchsia-pkg://fuchsia.com/netemul-sandbox-test#meta/dummy-proc-with-dev.cmx",
   "environment": {
      "test" : [{"arguments":["-P", "/vdev/class/network/ep"]}],
      "devices": ["ep"]
   },
   "networks": [{
       "name": "net",
       "endpoints": [{"name":"ep", "backing": "NETWORK_DEVICE"}]
   }]
}
)");
  RunSandboxSuccess();
}

}  // namespace testing
}  // namespace netemul
