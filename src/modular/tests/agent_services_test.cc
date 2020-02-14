// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/session/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <fuchsia/testing/modular/cpp/fidl.h>

#include <sdk/lib/modular/testing/cpp/fake_agent.h>

#include "gmock/gmock.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_module.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_session_shell.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_story_shell.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"

namespace {

const std::string kTestAgentUrl("fuchsia-pkg://fuchsia.com/fake_agent#meta/fake_agent.cmx");
const std::string kTestServiceName(fuchsia::testing::modular::TestProtocol::Name_);

// Configuration for testing |ComponentContext| ConnectToAgentService().
struct ConnectToAgentServiceTestConfig {
  // The map of |service_name|->|agent_url| used to look up a service
  // handler |agent_url| by name.
  std::map<std::string, std::string> service_to_agent_map;

  // If true, include the service_name in the |AgentServiceRequest|.
  // This is required for a successful connection.
  bool provide_service_name = false;

  // If true, include the specific handler (agent URL) in the
  // |AgentServiceRequest|. This is *not* required for a successful connection.
  bool provide_handler = false;

  // If true, include the service client-side channel in the
  // |AgentServiceRequest|. This is required for a successful connection.
  bool provide_channel = false;

  // If true, include the |AgentController| in the |AgentServiceRequest|.
  // This is required for a successful connection.
  bool provide_agent_controller = false;

  template <typename Interface>
  fuchsia::modular::AgentServiceRequest MakeAgentServiceRequest(
      std::string service_name, Interface service_request,
      fidl::InterfaceRequest<fuchsia::modular::AgentController> agent_controller) {
    fuchsia::modular::AgentServiceRequest agent_service_request;
    if (provide_service_name) {
      agent_service_request.set_service_name(service_name);
    }
    if (provide_handler) {
      agent_service_request.set_handler(kTestAgentUrl);
    }
    if (provide_channel) {
      agent_service_request.set_channel(service_request.TakeChannel());
    }
    if (provide_agent_controller) {
      agent_service_request.set_agent_controller(std::move(agent_controller));
    }
    return agent_service_request;
  }
};

class AgentServicesTest : public modular_testing::TestHarnessFixture {
 protected:
  AgentServicesTest() : fake_agent_(modular_testing::FakeAgent::CreateWithDefaultOptions()) {}

  fuchsia::modular::ComponentContextPtr StartTestHarness(
      ConnectToAgentServiceTestConfig test_config) {
    fuchsia::modular::testing::TestHarnessSpec spec;

    std::vector<fuchsia::modular::session::AgentServiceIndexEntry> agent_service_index;

    for (const auto& entry : test_config.service_to_agent_map) {
      fuchsia::modular::session::AgentServiceIndexEntry agent_service;
      agent_service.set_service_name(entry.first);
      agent_service.set_agent_url(entry.second);
      agent_service_index.emplace_back(std::move(agent_service));
    }

    spec.mutable_sessionmgr_config()->set_agent_service_index(std::move(agent_service_index));

    fuchsia::modular::testing::InterceptSpec intercept_spec;
    intercept_spec.set_component_url(kTestAgentUrl);
    spec.mutable_components_to_intercept()->push_back(std::move(intercept_spec));

    test_harness().events().OnNewComponent =
        [this](fuchsia::sys::StartupInfo startup_info,
               fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent> component) {
          ASSERT_EQ(startup_info.launch_info.url, kTestAgentUrl);
          fake_agent_->BuildInterceptOptions().launch_handler(std::move(startup_info),
                                                              component.Bind());
        };
    fit::function<void(fidl::InterfaceRequest<fuchsia::testing::modular::TestProtocol>)>
        service_handler =
            [this](fidl::InterfaceRequest<fuchsia::testing::modular::TestProtocol> request) {
              if (fake_agent_service_handler_) {
                fake_agent_service_handler_(std::move(request));
              }
            };
    fake_agent_->AddAgentService(std::move(service_handler));
    test_harness()->Run(std::move(spec));

    fuchsia::modular::ComponentContextPtr component_context;
    fuchsia::modular::testing::ModularService modular_service;
    modular_service.set_component_context(component_context.NewRequest());
    test_harness()->ConnectToModularService(std::move(modular_service));

    return component_context;
  }

  // Called by test functions to invoke ConnectToAgentService with various input configurations.
  //
  // |test_config| Input configurations and setup options.
  zx_status_t ExecuteConnectToAgentServiceTest(ConnectToAgentServiceTestConfig test_config) {
    fuchsia::modular::ComponentContextPtr component_context = StartTestHarness(test_config);

    // Client-side service pointer
    fuchsia::testing::modular::TestProtocolPtr service_ptr;
    auto service_name = kTestServiceName;
    auto service_request = service_ptr.NewRequest();
    zx_status_t service_status = ZX_OK;
    bool service_terminated = false;
    service_ptr.set_error_handler([&](zx_status_t status) {
      service_terminated = true;
      service_status = status;
    });

    bool got_request = false;
    fake_agent_service_handler_ =
        [&](fidl::InterfaceRequest<fuchsia::testing::modular::TestProtocol> request) {
          got_request = true;
        };

    fuchsia::modular::AgentControllerPtr agent_controller;
    auto agent_service_request = test_config.MakeAgentServiceRequest(
        service_name, std::move(service_request), agent_controller.NewRequest());
    component_context->ConnectToAgentService(std::move(agent_service_request));

    RunLoopUntil([&] { return got_request || service_terminated; });
    fake_agent_service_handler_ = nullptr;  // Callback references local variables.

    // Speed up teardown of the test by eagerly terminating the fake agent.
    fake_agent_->Exit(0);

    // If we got the service request, then routing of the agent service request was successful, even
    // though at this point we have already closed the service channel.
    if (got_request) {
      return ZX_OK;
    } else {
      EXPECT_NE(service_status, ZX_OK);
    }
    return service_status;
  }

  std::unique_ptr<modular_testing::FakeAgent> fake_agent_;
  fit::function<void(fidl::InterfaceRequest<fuchsia::testing::modular::TestProtocol>)>
      fake_agent_service_handler_;
};

// Ensure Session Manager's ConnectToAgentService can successfully find an
// agent for a given session name, and connect to that agent's service.
TEST_F(AgentServicesTest, ValidAndSuccessfulOneEntry) {
  ConnectToAgentServiceTestConfig test_config;
  test_config.provide_service_name = true;
  test_config.provide_channel = true;
  test_config.provide_agent_controller = true;

  test_config.service_to_agent_map = {
      {kTestServiceName, kTestAgentUrl},
  };

  EXPECT_EQ(ZX_OK, ExecuteConnectToAgentServiceTest(test_config));
}

// Find agent and service successfully among multiple index entries.
TEST_F(AgentServicesTest, ValidAndSuccessfulMultipleEntries) {
  ConnectToAgentServiceTestConfig test_config;
  test_config.provide_service_name = true;
  test_config.provide_channel = true;
  test_config.provide_agent_controller = true;

  test_config.service_to_agent_map = {
      {"chromium.cast.ApplicationConfigManager",
       "fuchsia-pkg://fuchsia.com/cast_agent#meta/cast_agent.cmx"},
      {kTestServiceName, kTestAgentUrl},
      {"fuchsia.feedback.DataProvider",
       "fuchsia-pkg://fuchsia.com/feedback_agent#meta/feedback_agent.cmx"},
  };

  EXPECT_EQ(ZX_OK, ExecuteConnectToAgentServiceTest(test_config));
}

// Find service successfully, from a specific handler. The index specifies this
// agent as the default handler, but should not be necessary.
TEST_F(AgentServicesTest, SpecificHandlerProvidedHasService) {
  ConnectToAgentServiceTestConfig test_config;
  test_config.provide_service_name = true;
  test_config.provide_handler = true;
  test_config.provide_channel = true;
  test_config.provide_agent_controller = true;

  test_config.service_to_agent_map = {
      {"chromium.cast.ApplicationConfigManager",
       "fuchsia-pkg://fuchsia.com/cast_agent#meta/cast_agent.cmx"},
      {kTestServiceName, kTestAgentUrl},
      {"fuchsia.feedback.DataProvider",
       "fuchsia-pkg://fuchsia.com/feedback_agent#meta/feedback_agent.cmx"},
  };

  EXPECT_EQ(ZX_OK, ExecuteConnectToAgentServiceTest(test_config));
}

// Find service successfully, from a specific handler. The index does not
// include the requested service, but it should not be needed since the
// handler is specified.
TEST_F(AgentServicesTest, SpecificHandlerProvidedHasServiceButNotInIndex) {
  ConnectToAgentServiceTestConfig test_config;
  test_config.provide_service_name = true;
  test_config.provide_handler = true;
  test_config.provide_channel = true;
  test_config.provide_agent_controller = true;

  test_config.service_to_agent_map = {
      {"chromium.cast.ApplicationConfigManager",
       "fuchsia-pkg://fuchsia.com/cast_agent#meta/cast_agent.cmx"},
      {"fuchsia.feedback.DataProvider",
       "fuchsia-pkg://fuchsia.com/feedback_agent#meta/feedback_agent.cmx"},
  };

  EXPECT_EQ(ZX_OK, ExecuteConnectToAgentServiceTest(test_config));
}

// Find service successfully, from a specific handler. The index specifies
// a different agent as the handler, but that agent should not be used since
// a specific agent was specified.
TEST_F(AgentServicesTest, SpecificHandlerProvidedHasServiceButIndexHasDifferentHandler) {
  ConnectToAgentServiceTestConfig test_config;
  test_config.provide_service_name = true;
  test_config.provide_handler = true;
  test_config.provide_channel = true;
  test_config.provide_agent_controller = true;

  test_config.service_to_agent_map = {
      {kTestServiceName, "fuchsia-pkg://fuchsia.com/cast_agent#meta/cast_agent.cmx"},
      {"fuchsia.feedback.DataProvider",
       "fuchsia-pkg://fuchsia.com/feedback_agent#meta/feedback_agent.cmx"},
  };

  EXPECT_EQ(ZX_OK, ExecuteConnectToAgentServiceTest(test_config));
}

// Bad request
TEST_F(AgentServicesTest, NoServiceNameProvided) {
  ConnectToAgentServiceTestConfig test_config;
  // test_config.provide_service_name = true;
  test_config.provide_channel = true;
  test_config.provide_agent_controller = true;

  test_config.service_to_agent_map = {
      {"fuchsia.feedback.DataProvider",
       "fuchsia-pkg://fuchsia.com/feedback_agent#meta/feedback_agent.cmx"},
  };

  EXPECT_EQ(ZX_ERR_PEER_CLOSED, ExecuteConnectToAgentServiceTest(test_config));
}

// Bad request
TEST_F(AgentServicesTest, NoChannelProvided) {
  ConnectToAgentServiceTestConfig test_config;
  test_config.provide_service_name = true;
  // test_config.provide_channel = true;
  test_config.provide_agent_controller = true;

  EXPECT_EQ(ZX_ERR_PEER_CLOSED, ExecuteConnectToAgentServiceTest(test_config));
}

// Bad request
TEST_F(AgentServicesTest, NoAgentControllerProvided) {
  ConnectToAgentServiceTestConfig test_config;
  test_config.provide_service_name = true;
  test_config.provide_channel = true;
  // test_config.provide_agent_controller = true;

  EXPECT_EQ(ZX_ERR_PEER_CLOSED, ExecuteConnectToAgentServiceTest(test_config));
}

// Attempt to look up the agent based on the service name, but it is not in
// the index.
TEST_F(AgentServicesTest, NoHandlerForService) {
  ConnectToAgentServiceTestConfig test_config;
  test_config.provide_service_name = true;
  test_config.provide_channel = true;
  test_config.provide_agent_controller = true;

  EXPECT_EQ(ZX_ERR_NOT_FOUND, ExecuteConnectToAgentServiceTest(test_config));
}

class AgentServicesSFWCompatTest : public modular_testing::TestHarnessFixture {
 protected:
  AgentServicesSFWCompatTest() {}

  fuchsia::modular::testing::TestHarnessSpec CreateSpecWithAgentServiceIndex(
      std::map<std::string, std::string> agent_service_index_map) {
    fuchsia::modular::testing::TestHarnessSpec spec;
    std::vector<fuchsia::modular::session::AgentServiceIndexEntry> agent_service_index;
    for (const auto& entry : agent_service_index_map) {
      fuchsia::modular::session::AgentServiceIndexEntry agent_service;
      agent_service.set_service_name(entry.first);
      agent_service.set_agent_url(entry.second);
      agent_service_index.emplace_back(std::move(agent_service));
    }
    spec.mutable_sessionmgr_config()->set_agent_service_index(std::move(agent_service_index));
    return spec;
  }

  modular_testing::TestHarnessBuilder::InterceptOptions AddSandboxServices(
      std::vector<std::string> service_names,
      modular_testing::TestHarnessBuilder::InterceptOptions options) {
    for (auto service_name : service_names) {
      options.sandbox_services.push_back(service_name);
    }
    return options;
  }
};

// A version of FakeComponent, behaviorally similar to FakeAgent, with the added behavior of
// capturing the `requestor_url` parameter of Agent.Connect() calls and exposing them through
// `requestor_urls()`.
class RequestorIdCapturingAgent : public modular_testing::FakeComponent,
                                  fuchsia::modular::Agent,
                                  fuchsia::sys::ServiceProvider {
 public:
  RequestorIdCapturingAgent(modular_testing::FakeComponent::Args args)
      : FakeComponent(std::move(args)) {}

  static std::unique_ptr<RequestorIdCapturingAgent> CreateWithDefaultOptions() {
    return std::make_unique<RequestorIdCapturingAgent>(modular_testing::FakeComponent::Args{
        .url = modular_testing::TestHarnessBuilder::GenerateFakeUrl()});
  }

  std::vector<std::string> requestor_urls() const { return requestor_urls_; }

  template <typename Interface>
  void AddAgentService(fidl::InterfaceRequestHandler<Interface> handler) {
    buffered_add_agent_service_calls_.push_back([this, handler = std::move(handler)]() mutable {
      service_name_to_handler_[Interface::Name_] = [handler = std::move(handler)](zx::channel req) {
        handler(fidl::InterfaceRequest<Interface>(std::move(req)));
      };
    });

    FlushAddAgentServiceIfRunning();
  }

 private:
  // |modular_testing::FakeComponent|
  void OnCreate(fuchsia::sys::StartupInfo startup_info) {
    component_context()->outgoing()->AddPublicService<fuchsia::modular::Agent>(
        agent_bindings_.GetHandler(this));
    FlushAddAgentServiceIfRunning();
  }

  // |fuchsia::modular::Agent|
  void Connect(
      std::string requestor_id,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> outgoing_services_request) override {
    requestor_urls_.push_back(requestor_id);
    agent_service_provider_bindings_.AddBinding(this, std::move(outgoing_services_request));
  }

  // |fuchsia::sys::ServiceProvider|
  void ConnectToService(std::string service_name, zx::channel request) override {
    auto it = service_name_to_handler_.find(service_name);
    if (it != service_name_to_handler_.end()) {
      it->second(std::move(request));
    }
  }

  void FlushAddAgentServiceIfRunning() {
    if (is_running()) {
      for (auto& call : buffered_add_agent_service_calls_) {
        call();
      }
      buffered_add_agent_service_calls_.clear();
    }
  }

  std::vector<std::string> requestor_urls_;

  fidl::BindingSet<fuchsia::modular::Agent> agent_bindings_;
  fidl::BindingSet<fuchsia::sys::ServiceProvider> agent_service_provider_bindings_;

  // A mapping of `service name -> service connection handle` which is inflated using
  // AddService<>().
  std::unordered_map<std::string, fit::function<void(zx::channel)>> service_name_to_handler_;
  std::vector<fit::closure> buffered_add_agent_service_calls_;
};

// Test that an Agent service can be acquired from any of another Agent, a Module, Session or Story
// Shells, including testing that calls to the Agent.Connect() method (implemented by the agent)
// result in the correct requestor ids, even if those clients connect via their environment.
TEST_F(AgentServicesSFWCompatTest, ConnectToService_Success) {
  auto serving_agent = RequestorIdCapturingAgent::CreateWithDefaultOptions();

  // Intercept the following components in order to test their access to agent services
  // through their respective environments.
  auto agent = modular_testing::FakeAgent::CreateWithDefaultOptions();
  auto session_shell = modular_testing::FakeSessionShell::CreateWithDefaultOptions();
  auto story_shell = modular_testing::FakeStoryShell::CreateWithDefaultOptions();
  auto module = modular_testing::FakeModule::CreateWithDefaultOptions();

  // Set up the test environment with TestProtocol being served by `serving_agent`.
  auto spec = CreateSpecWithAgentServiceIndex(
      {{fuchsia::testing::modular::TestProtocol::Name_, serving_agent->url()}});
  spec.mutable_sessionmgr_config()->mutable_session_agents()->push_back(agent->url());

  modular_testing::TestHarnessBuilder builder(std::move(spec));
  builder.InterceptComponent(serving_agent->BuildInterceptOptions());
  builder.InterceptComponent(AddSandboxServices({fuchsia::testing::modular::TestProtocol::Name_},
                                                agent->BuildInterceptOptions()));
  builder.InterceptSessionShell(AddSandboxServices({fuchsia::testing::modular::TestProtocol::Name_},
                                                   session_shell->BuildInterceptOptions()));
  builder.InterceptStoryShell(AddSandboxServices({fuchsia::testing::modular::TestProtocol::Name_},
                                                 story_shell->BuildInterceptOptions()));
  builder.InterceptComponent(AddSandboxServices({fuchsia::testing::modular::TestProtocol::Name_},
                                                module->BuildInterceptOptions()));

  // Instruct `serving_agent` to serve the TestProtocol, tracking the number of times
  // the service was successfully connected.
  int num_connections = 0;
  std::vector<zx::channel> protocol_requests;
  serving_agent->AddAgentService(
      fit::function<void(fidl::InterfaceRequest<fuchsia::testing::modular::TestProtocol>)>(
          [&](fidl::InterfaceRequest<fuchsia::testing::modular::TestProtocol> request) {
            ++num_connections;
            protocol_requests.push_back(request.TakeChannel());
          }));
  builder.BuildAndRun(test_harness());

  RunLoopUntil([&] { return agent->is_running() && session_shell->is_running(); });
  ASSERT_FALSE(serving_agent->is_running());

  // Create a story so that the story shell and module are both run.
  fuchsia::modular::Intent intent;
  intent.handler = module->url();
  intent.action = "action";
  modular_testing::AddModToStory(test_harness(), "storyName", "modName", std::move(intent));
  RunLoopUntil([&] { return story_shell->is_running() && module->is_running(); });

  // Attempt to connect to the test service from all of our different components.
  std::vector<fuchsia::testing::modular::TestProtocolPtr> protocol_ptrs;
  protocol_ptrs.push_back(
      agent->component_context()->svc()->Connect<fuchsia::testing::modular::TestProtocol>());
  protocol_ptrs.push_back(session_shell->component_context()
                              ->svc()
                              ->Connect<fuchsia::testing::modular::TestProtocol>());
  protocol_ptrs.push_back(
      story_shell->component_context()->svc()->Connect<fuchsia::testing::modular::TestProtocol>());
  protocol_ptrs.push_back(
      module->component_context()->svc()->Connect<fuchsia::testing::modular::TestProtocol>());

  // Track the number of those connection attempts failed.
  int num_errors = 0;
  for (auto& ptr : protocol_ptrs) {
    ptr.set_error_handler([&](zx_status_t) { ++num_errors; });
  }

  constexpr int kTotalRequests = 4;
  RunLoopUntil([&] { return num_connections + num_errors == kTotalRequests; });
  EXPECT_TRUE(serving_agent->is_running());
  EXPECT_EQ(num_connections, kTotalRequests);
  EXPECT_EQ(num_errors, 0);
  EXPECT_THAT(serving_agent->requestor_urls(),
              testing::UnorderedElementsAre(agent->url(), session_shell->url(), story_shell->url(),
                                            /* `module` path */ "modName"));
}

// Test that when a component tries to connect to a service through its environment,
// but the agent that serves that service can't be launched, an error is returned.
TEST_F(AgentServicesSFWCompatTest, ConnectToService_FailAgentNotPresent) {
  auto agent = modular_testing::FakeAgent::CreateWithDefaultOptions();

  // Set up the test environment with TestProtocol being served by `serving_agent`, but
  // under a name that will not match when `agent` tries to connect.
  auto spec = CreateSpecWithAgentServiceIndex(
      {{fuchsia::testing::modular::TestProtocol::Name_, "fuchsia-pkg://fuchsia.com/not/found"}});
  spec.mutable_sessionmgr_config()->mutable_session_agents()->push_back(agent->url());

  modular_testing::TestHarnessBuilder builder(std::move(spec));
  builder.InterceptComponent(AddSandboxServices({fuchsia::testing::modular::TestProtocol::Name_},
                                                agent->BuildInterceptOptions()));
  builder.BuildAndRun(test_harness());
  RunLoopUntil([&] { return agent->is_running(); });

  // Attempt to connect to the test service.
  bool saw_error = false;
  zx_status_t status = ZX_OK;
  auto ptr = agent->component_context()->svc()->Connect<fuchsia::testing::modular::TestProtocol>();
  ptr.set_error_handler([&](zx_status_t s) {
    saw_error = true;
    status = s;
  });

  RunLoopUntil([&] { return saw_error; });
  // appmgr / sysmgr result in a peer closed error.
  EXPECT_EQ(status, ZX_ERR_PEER_CLOSED);
}

// Test that when a component tries to connect to a service through its environment,
// but that service is not available, the client encounters an error.
TEST_F(AgentServicesSFWCompatTest, ConnectToService_FailNoAgentMapping) {
  auto serving_agent = modular_testing::FakeAgent::CreateWithDefaultOptions();
  auto agent = modular_testing::FakeAgent::CreateWithDefaultOptions();

  // Set up the test environment with TestProtocol being served by `serving_agent`, but
  // under a name that will not match when `agent` tries to connect.
  auto spec =
      CreateSpecWithAgentServiceIndex({{"fuchsia.testing.modular.NotFound", serving_agent->url()}});
  spec.mutable_sessionmgr_config()->mutable_session_agents()->push_back(agent->url());

  modular_testing::TestHarnessBuilder builder(std::move(spec));
  builder.InterceptComponent(serving_agent->BuildInterceptOptions());
  builder.InterceptComponent(AddSandboxServices({fuchsia::testing::modular::TestProtocol::Name_},
                                                agent->BuildInterceptOptions()));

  // Instruct `serving_agent` to serve the TestProtocol.
  serving_agent->AddAgentService(
      fit::function<void(fidl::InterfaceRequest<fuchsia::testing::modular::TestProtocol>)>(
          [&](fidl::InterfaceRequest<fuchsia::testing::modular::TestProtocol> request) {
            FAIL() << "Did not expect service connection request to reach the agent.";
          }));
  builder.BuildAndRun(test_harness());

  RunLoopUntil([&] { return agent->is_running(); });
  ASSERT_FALSE(serving_agent->is_running());

  // Attempt to connect to the test service.
  bool saw_error = false;
  zx_status_t status = ZX_OK;
  auto ptr = agent->component_context()->svc()->Connect<fuchsia::testing::modular::TestProtocol>();
  ptr.set_error_handler([&](zx_status_t s) {
    saw_error = true;
    status = s;
  });
  RunLoopUntil([&] { return saw_error; });
  EXPECT_FALSE(serving_agent->is_running());
  // appmgr / sysmgr result in a peer closed error.
  EXPECT_EQ(status, ZX_ERR_PEER_CLOSED);
}

}  // namespace
