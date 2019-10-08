// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/testing/cpp/fidl.h>

#include "fuchsia/modular/session/cpp/fidl.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"

namespace {

const std::string kTestAgentUrl(
    "fuchsia-pkg://fuchsia.com/clipboard_agent#meta/clipboard_agent.cmx");
const std::string kTestServiceName(fuchsia::modular::Clipboard::Name_);

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

// Expected test results.
struct ConnectToAgentServiceExpect {
  // If true, the test should connect to the test agent and the requested
  // named service, send a test value, "peek" at the current value, and verify
  // the value is the same.
  bool got_peek_content = false;

  // If set to an error code, the service channel should receive the given
  // error.
  zx_status_t service_status = ZX_OK;
};

class AgentServicesTest : public modular_testing::TestHarnessFixture {
 protected:
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

    test_harness()->Run(std::move(spec));

    // Create a new story -- this should auto-start the story (because of
    // test_session_shell's behaviour), and launch a new story shell.
    fuchsia::modular::ComponentContextPtr component_context;
    fuchsia::modular::testing::ModularService modular_service;
    modular_service.set_component_context(component_context.NewRequest());
    test_harness()->ConnectToModularService(std::move(modular_service));

    return component_context;
  }

  // Called by test functions to test various input configurations.
  // |test_config| Input configurations and setup options.
  // |expect| Expected results to compare to actual results for the test to
  // confirm.
  void ExecuteConnectToAgentServiceTest(ConnectToAgentServiceTestConfig test_config,
                                        ConnectToAgentServiceExpect expect) {
    const std::string kTestContent("Test clipboard content");

    fuchsia::modular::ComponentContextPtr component_context = StartTestHarness(test_config);

    // Client-side service pointer
    fuchsia::modular::ClipboardPtr clipboard_service_ptr;
    auto service_name = clipboard_service_ptr->Name_;
    auto service_request = clipboard_service_ptr.NewRequest();
    zx_status_t service_status = ZX_OK;
    clipboard_service_ptr.set_error_handler(
        [&service_status](zx_status_t status) { service_status = status; });

    // standard AgentController initialization
    fuchsia::modular::AgentControllerPtr agent_controller;
    zx_status_t agent_controller_status = ZX_OK;
    agent_controller.set_error_handler(
        [&agent_controller_status](zx_status_t status) { agent_controller_status = status; });

    auto agent_service_request = test_config.MakeAgentServiceRequest(
        service_name, std::move(service_request), agent_controller.NewRequest());
    component_context->ConnectToAgentService(std::move(agent_service_request));

    clipboard_service_ptr->Push(kTestContent);
    bool got_peek_content = false;
    clipboard_service_ptr->Peek([&](fidl::StringPtr content) {
      ASSERT_TRUE(content.has_value());
      got_peek_content = true;
      EXPECT_EQ(content.value(), kTestContent);
    });

    RunLoopUntil([&] {
      return got_peek_content || service_status != ZX_OK ||
             (expect.service_status == ZX_OK && agent_controller_status != ZX_OK);
      // The order of error callbacks is non-deterministic. If checking for a
      // specific service error, wait for it.
    });

    EXPECT_EQ(got_peek_content, expect.got_peek_content);
    EXPECT_EQ(service_status, expect.service_status);
  }
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

  ConnectToAgentServiceExpect expect;
  expect.got_peek_content = true;

  ExecuteConnectToAgentServiceTest(test_config, expect);
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

  ConnectToAgentServiceExpect expect;
  expect.got_peek_content = true;

  ExecuteConnectToAgentServiceTest(test_config, expect);
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

  ConnectToAgentServiceExpect expect;
  expect.got_peek_content = true;

  ExecuteConnectToAgentServiceTest(test_config, expect);
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

  ConnectToAgentServiceExpect expect;
  expect.got_peek_content = true;

  ExecuteConnectToAgentServiceTest(test_config, expect);
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

  ConnectToAgentServiceExpect expect;
  expect.got_peek_content = true;

  ExecuteConnectToAgentServiceTest(test_config, expect);
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

  ConnectToAgentServiceExpect expect;
  expect.service_status = ZX_ERR_PEER_CLOSED;

  ExecuteConnectToAgentServiceTest(test_config, expect);
}

// Bad request
TEST_F(AgentServicesTest, NoChannelProvided) {
  ConnectToAgentServiceTestConfig test_config;
  test_config.provide_service_name = true;
  // test_config.provide_channel = true;
  test_config.provide_agent_controller = true;

  ConnectToAgentServiceExpect expect;
  expect.service_status = ZX_ERR_PEER_CLOSED;

  ExecuteConnectToAgentServiceTest(test_config, expect);
}

// Bad request
TEST_F(AgentServicesTest, NoAgentControllerProvided) {
  ConnectToAgentServiceTestConfig test_config;
  test_config.provide_service_name = true;
  test_config.provide_channel = true;
  // test_config.provide_agent_controller = true;

  ConnectToAgentServiceExpect expect;
  expect.service_status = ZX_ERR_PEER_CLOSED;

  ExecuteConnectToAgentServiceTest(test_config, expect);
}

// Attempt to look up the agent based on the service name, but it is not in
// the index.
TEST_F(AgentServicesTest, NoHandlerForService) {
  ConnectToAgentServiceTestConfig test_config;
  test_config.provide_service_name = true;
  test_config.provide_channel = true;
  test_config.provide_agent_controller = true;

  ConnectToAgentServiceExpect expect;
  expect.service_status = ZX_ERR_NOT_FOUND;

  ExecuteConnectToAgentServiceTest(test_config, expect);
}

}  // namespace
