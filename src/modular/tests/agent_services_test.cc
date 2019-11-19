// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/session/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <fuchsia/testing/modular/cpp/fidl.h>

#include <sdk/lib/modular/testing/cpp/fake_agent.h>

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

}  // namespace
