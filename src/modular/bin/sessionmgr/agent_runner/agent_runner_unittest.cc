// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/agent_runner/agent_runner.h"

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/testing/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/svc/cpp/service_namespace.h>
#include <lib/sys/cpp/testing/fake_launcher.h>
#include <lib/zx/object.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <memory>

#include <fs/service.h>
#include <fs/synchronous_vfs.h>
#include <gtest/gtest.h>

#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/macros.h"
#include "src/modular/lib/fidl/app_client.h"
#include "src/modular/lib/fidl/array_to_string.h"
#include "src/modular/lib/pseudo_dir/pseudo_dir_server.h"

namespace modular_testing {
namespace {

using ::sys::testing::FakeLauncher;

// The choice of "TestProtocol" as the test service is arbitrary, but the
// ConnectToAgentService() tests require an existing service type.
constexpr char kTestAgentUrl[] = "file:///my_agent";

static zx_koid_t get_object_koid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  if (zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL) != ZX_OK) {
    return 0;
  }
  return info.koid;
}

class TestAgent : fuchsia::modular::Agent,
                  fuchsia::modular::Lifecycle,
                  public fuchsia::sys::ComponentController {
 public:
  TestAgent(zx::channel directory_request,
            fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl,
            std::unique_ptr<component::ServiceNamespace> services_ptr = nullptr,
            bool serve_lifecycle_protocol = false)
      : controller_(this, std::move(ctrl)),
        agent_binding_(this),
        lifecycle_binding_(this),
        services_ptr_(std::move(services_ptr)) {
    auto outgoing_dir = std::make_unique<vfs::PseudoDir>();
    outgoing_dir_ptr_ = outgoing_dir.get();
    outgoing_dir->AddEntry(
        fuchsia::modular::Agent::Name_,
        std::make_unique<vfs::Service>([this](zx::channel channel, async_dispatcher_t* /*unused*/) {
          agent_binding_.Bind(std::move(channel));
        }));
    if (serve_lifecycle_protocol) {
      outgoing_dir->AddEntry(fuchsia::modular::Lifecycle::Name_,
                             std::make_unique<vfs::Service>(
                                 [this](zx::channel channel, async_dispatcher_t* /*unused*/) {
                                   lifecycle_binding_.Bind(std::move(channel));
                                 }));
    }
    outgoing_dir_server_ = std::make_unique<modular::PseudoDirServer>(std::move(outgoing_dir));
    outgoing_dir_server_->Serve(std::move(directory_request));
    controller_.set_error_handler(
        [this](zx_status_t /*unused*/) { controller_connected_ = false; });
  }

  template <class Interface>
  void AddOutgoingService(fidl::InterfaceRequestHandler<Interface> handler) {
    outgoing_dir_ptr_->AddEntry(
        Interface::Name_,
        std::make_unique<vfs::Service>(
            [handler = std::move(handler)](zx::channel channel, async_dispatcher_t* /*unused*/) {
              handler(fidl::InterfaceRequest<Interface>(std::move(channel)));
            }));
  }

  void KillApplication() { controller_.Unbind(); }

  int connect_call_count() { return connect_call_count_; }

  bool lifecycle_terminate_called() { return lifecycle_terminate_called_; }

  bool controller_connected() { return controller_connected_; }

 private:
  // |ComponentController|
  void Kill() override { FX_NOTREACHED(); }
  // |ComponentController|
  void Detach() override { FX_NOTREACHED(); }

  // |fuchsia::modular::Agent|
  void Connect(std::string requestor_url,
               fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> outgoing_services) override {
    ++connect_call_count_;
    if (services_ptr_) {
      services_ptr_->AddBinding(std::move(outgoing_services));
    }
  }

  // |fuchsia::modular::Lifecycle|
  void Terminate() override {
    lifecycle_terminate_called_ = true;
    controller_connected_ = false;
    controller_.Close(ZX_OK);
  }

 private:
  fidl::Binding<fuchsia::sys::ComponentController> controller_;
  fidl::Binding<fuchsia::modular::Agent> agent_binding_;
  fidl::Binding<fuchsia::modular::Lifecycle> lifecycle_binding_;
  std::unique_ptr<component::ServiceNamespace> services_ptr_;

  // `outgoing_dir_server_` must be initialized after `agent_binding_` (which itself serves
  // `services_ptr_`) so that it is guaranteed to be destroyed *before* `agent_binding_` to protect
  // access to `services_ptr_`. See fxbug.dev/49304.
  std::unique_ptr<modular::PseudoDirServer> outgoing_dir_server_;
  vfs::PseudoDir* outgoing_dir_ptr_;

  // The number of times Connect() has been called.
  int connect_call_count_ = 0;

  // When true, the agent has been gracefully torn down via |fuchsia::modular::Lifecycle|.
  bool lifecycle_terminate_called_ = false;

  // When true, the ComponentController is bound. AgentContextImpl disconnects from
  // ComponentController when it wants to terminate the agent, which sets this to false.
  // This is also set to false when the agent is gracefully torn down in Terminate.
  bool controller_connected_ = true;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestAgent);
};

class AgentRunnerTest : public gtest::RealLoopFixture {
 public:
  AgentRunnerTest() = default;

  void SetUp() override { gtest::RealLoopFixture::SetUp(); }

  void TearDown() override {
    agent_runner_.reset();
    gtest::RealLoopFixture::TearDown();
  }

 protected:
  modular::AgentRunner* GetOrCreateAgentRunner() {
    if (agent_runner_ == nullptr) {
      agent_runner_ = std::make_unique<modular::AgentRunner>(
          &launcher_, /*agent_services_factory=*/nullptr, &node_, /*on_critical_agent_crash=*/
          [this] {
            if (on_session_restart_callback_) {
              on_session_restart_callback_();
            }
          },
          std::move(agent_service_index_),
          /*session_agents=*/std::vector<std::string>(),
          std::move(restart_session_on_agent_crash_));
    }
    return agent_runner_.get();
  }

  void set_agent_service_index(std::map<std::string, std::string> agent_service_index) {
    agent_service_index_ = std::move(agent_service_index);
  }

  void set_restart_session_on_agent_crash(std::vector<std::string> restart_session_on_agent_crash) {
    restart_session_on_agent_crash_ = std::move(restart_session_on_agent_crash);
  }

  void set_on_session_restart_callback(std::function<void()> on_session_restart_callback) {
    on_session_restart_callback_ = std::move(on_session_restart_callback);
  }

  FakeLauncher* launcher() { return &launcher_; }

 private:
  FakeLauncher launcher_;
  inspect::Node node_;

  files::ScopedTempDir mq_data_dir_;
  std::unique_ptr<modular::AgentRunner> agent_runner_;
  std::map<std::string, std::string> agent_service_index_;
  std::vector<std::string> restart_session_on_agent_crash_;
  std::function<void()> on_session_restart_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AgentRunnerTest);
};

}  // namespace

// Test that connecting to an agent will start it up.
// Then there should be an fuchsia::modular::Agent.Connect().
TEST_F(AgentRunnerTest, ConnectToAgent) {
  int agent_launch_count = 0;
  std::unique_ptr<TestAgent> test_agent;
  launcher()->RegisterComponent(
      kTestAgentUrl, [&test_agent, &agent_launch_count](
                         fuchsia::sys::LaunchInfo launch_info,
                         fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        test_agent =
            std::make_unique<TestAgent>(std::move(launch_info.directory_request), std::move(ctrl));
        ++agent_launch_count;
      });

  fuchsia::sys::ServiceProviderPtr incoming_services;
  fuchsia::modular::AgentControllerPtr agent_controller;
  GetOrCreateAgentRunner()->ConnectToAgent("requestor_url", kTestAgentUrl,
                                           incoming_services.NewRequest(),
                                           agent_controller.NewRequest());

  RunLoopUntil([&test_agent] { return test_agent && test_agent->connect_call_count() > 0; });
  EXPECT_EQ(1, agent_launch_count);
  EXPECT_EQ(1, test_agent->connect_call_count());

  // Connecting to the same agent again shouldn't launch a new instance and
  // shouldn't re-initialize the existing instance of the agent application,
  // but should call |Connect()|.

  fuchsia::modular::AgentControllerPtr agent_controller2;
  fuchsia::sys::ServiceProviderPtr incoming_services2;
  GetOrCreateAgentRunner()->ConnectToAgent("requestor_url2", kTestAgentUrl,
                                           incoming_services2.NewRequest(),
                                           agent_controller2.NewRequest());
  RunLoopUntil([&test_agent] { return test_agent && test_agent->connect_call_count() > 1; });
  EXPECT_EQ(1, agent_launch_count);
  EXPECT_EQ(2, test_agent->connect_call_count());
}

// Test that if an agent application dies, it is removed from agent runner
// (which means outstanding AgentControllers are closed).
TEST_F(AgentRunnerTest, AgentController) {
  std::unique_ptr<TestAgent> test_agent;
  launcher()->RegisterComponent(
      kTestAgentUrl, [&test_agent](fuchsia::sys::LaunchInfo launch_info,
                                   fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        test_agent =
            std::make_unique<TestAgent>(std::move(launch_info.directory_request), std::move(ctrl));
      });

  fuchsia::sys::ServiceProviderPtr incoming_services;
  fuchsia::modular::AgentControllerPtr agent_controller;
  GetOrCreateAgentRunner()->ConnectToAgent("requestor_url", kTestAgentUrl,
                                           incoming_services.NewRequest(),
                                           agent_controller.NewRequest());

  RunLoopUntil([&test_agent] { return !!test_agent; });
  test_agent->KillApplication();

  // fuchsia::modular::Agent application died, so check that
  // fuchsia::modular::AgentController dies here.
  agent_controller.set_error_handler(
      [&agent_controller](zx_status_t status) { agent_controller.Unbind(); });
  RunLoopUntil([&agent_controller] { return !agent_controller.is_bound(); });
  EXPECT_FALSE(agent_controller.is_bound());
}

// AgentServiceRequest.service_name is required: when not provided, expect an error
// in the epitaph for the service channel.
TEST_F(AgentRunnerTest, ConnectToAgentService_NoServiceNameInAgentServiceRequest) {
  fuchsia::testing::modular::TestProtocolPtr service_ptr;

  zx_status_t status;
  bool channel_closed = false;
  service_ptr.set_error_handler([&](zx_status_t s) {
    channel_closed = true;
    status = s;
  });

  // The agent should never be launched.
  launcher()->RegisterComponent(
      kTestAgentUrl,
      [](fuchsia::sys::LaunchInfo launch_info,
         fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) { FX_NOTREACHED(); });

  fuchsia::modular::AgentServiceRequest request;
  request.set_handler(kTestAgentUrl);
  request.set_channel(service_ptr.NewRequest().TakeChannel());
  GetOrCreateAgentRunner()->ConnectToAgentService("requestor_url", std::move(request));

  RunLoopUntil([&] { return channel_closed; });
  EXPECT_EQ(status, ZX_ERR_PEER_CLOSED);
}

// AgentServiceRequest.channel is required: when not provided, expect an error on
// the AgentController.
TEST_F(AgentRunnerTest, ConnectToAgentService_NoChannelInAgentServiceRequest) {
  fuchsia::modular::AgentControllerPtr agent_controller;

  zx_status_t status;
  bool channel_closed = false;
  agent_controller.set_error_handler([&](zx_status_t s) {
    channel_closed = true;
    status = s;
  });

  // The agent should never be launched.
  launcher()->RegisterComponent(
      kTestAgentUrl,
      [](fuchsia::sys::LaunchInfo launch_info,
         fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) { FX_NOTREACHED(); });

  fuchsia::modular::AgentServiceRequest request;
  request.set_handler(kTestAgentUrl);
  request.set_service_name(fuchsia::testing::modular::TestProtocol::Name_);
  request.set_agent_controller(agent_controller.NewRequest());
  GetOrCreateAgentRunner()->ConnectToAgentService("requestor_url", std::move(request));

  RunLoopUntil([&] { return channel_closed; });
  EXPECT_EQ(status, ZX_ERR_PEER_CLOSED);
}

// When no handler is provided, and the agent_service_index does not contain
// an entry for this service, expect a ZX_ERR_NOT_FOUND on the service channel.
TEST_F(AgentRunnerTest, ConnectToAgentService_NoAgentForServiceName) {
  fuchsia::testing::modular::TestProtocolPtr service_ptr;

  zx_status_t status;
  bool channel_closed = false;
  service_ptr.set_error_handler([&](zx_status_t s) {
    channel_closed = true;
    status = s;
  });

  // The agent should never be launched.
  launcher()->RegisterComponent(
      kTestAgentUrl,
      [](fuchsia::sys::LaunchInfo launch_info,
         fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) { FX_NOTREACHED(); });

  set_agent_service_index({{"different_service", kTestAgentUrl}});
  fuchsia::modular::AgentServiceRequest request;
  request.set_service_name(fuchsia::testing::modular::TestProtocol::Name_);
  request.set_channel(service_ptr.NewRequest().TakeChannel());
  GetOrCreateAgentRunner()->ConnectToAgentService("requestor_url", std::move(request));

  RunLoopUntil([&] { return channel_closed; });
  EXPECT_EQ(status, ZX_ERR_NOT_FOUND);
}

// ConnectToAgentService, when successful, should launch the agent specified in the
// agent_service_index, and provide it with the channel endpoint in AgentServiceRequest.channel.
TEST_F(AgentRunnerTest, ConnectToAgentService_ConnectToServiceNameSuccess) {
  fuchsia::testing::modular::TestProtocolPtr service_ptr;
  auto service_ptr_request = service_ptr.NewRequest();

  zx_status_t status;
  bool channel_closed = false;
  service_ptr.set_error_handler([&](zx_status_t s) {
    channel_closed = true;
    status = s;
  });

  // Create a ServiceNamespace with the test service in it.
  auto service_namespace = std::make_unique<component::ServiceNamespace>();
  bool agent_got_service_request = false;
  service_namespace->AddService<fuchsia::testing::modular::TestProtocol>(
      [&agent_got_service_request,
       cached_service_request_koid = get_object_koid(service_ptr_request.channel().get())](
          fidl::InterfaceRequest<fuchsia::testing::modular::TestProtocol> request) {
        // Expect the same channel object that was originally provided in AgentServiceRequest.
        EXPECT_EQ(get_object_koid(request.channel().get()), cached_service_request_koid);
        agent_got_service_request = true;
      });

  // Register to intercept the agent launch.
  std::unique_ptr<TestAgent> test_agent;
  launcher()->RegisterComponent(
      kTestAgentUrl, [&](fuchsia::sys::LaunchInfo launch_info,
                         fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        test_agent = std::make_unique<TestAgent>(std::move(launch_info.directory_request),
                                                 std::move(ctrl), std::move(service_namespace));
      });

  set_agent_service_index({{fuchsia::testing::modular::TestProtocol::Name_, kTestAgentUrl}});
  fuchsia::modular::AgentServiceRequest request;
  request.set_service_name(fuchsia::testing::modular::TestProtocol::Name_);
  request.set_channel(service_ptr_request.TakeChannel());
  GetOrCreateAgentRunner()->ConnectToAgentService("requestor_url", std::move(request));

  RunLoopUntil([&] { return agent_got_service_request; });
}

// Test that adding an agent that is already running (as encapsulated by an AppClient<> instance)
// can serve services.
TEST_F(AgentRunnerTest, AddRunningAgent_CanConnectToAgentService) {
  fuchsia::testing::modular::TestProtocolPtr service_ptr;
  auto service_ptr_request = service_ptr.NewRequest();

  // Create a ServiceNamespace with the test service in it.
  auto service_namespace = std::make_unique<component::ServiceNamespace>();
  bool agent_got_service_request = false;
  service_namespace->AddService<fuchsia::testing::modular::TestProtocol>(
      [&agent_got_service_request,
       cached_service_request_koid = get_object_koid(service_ptr_request.channel().get())](
          fidl::InterfaceRequest<fuchsia::testing::modular::TestProtocol> request) {
        // Expect the same channel object that was originally provided in AgentServiceRequest.
        EXPECT_EQ(get_object_koid(request.channel().get()), cached_service_request_koid);
        agent_got_service_request = true;
      });

  // Register to intercept the agent launch.
  std::unique_ptr<TestAgent> test_agent;
  launcher()->RegisterComponent(
      kTestAgentUrl, [&](fuchsia::sys::LaunchInfo launch_info,
                         fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        FX_CHECK(!test_agent);  // Should not launch agent more than once!
        test_agent = std::make_unique<TestAgent>(std::move(launch_info.directory_request),
                                                 std::move(ctrl), std::move(service_namespace));
      });

  set_agent_service_index({{fuchsia::testing::modular::TestProtocol::Name_, kTestAgentUrl}});
  auto agent_runner = GetOrCreateAgentRunner();

  fuchsia::modular::session::AppConfig agent_app_config;
  agent_app_config.set_url(kTestAgentUrl);
  auto agent_app_client = std::make_unique<modular::AppClient<fuchsia::modular::Lifecycle>>(
      launcher(), std::move(agent_app_config));
  agent_runner->AddRunningAgent(kTestAgentUrl, std::move(agent_app_client));

  fuchsia::modular::AgentServiceRequest request;
  request.set_service_name(fuchsia::testing::modular::TestProtocol::Name_);
  request.set_channel(service_ptr_request.TakeChannel());
  agent_runner->ConnectToAgentService("requestor_url", std::move(request));

  RunLoopUntil([&] { return agent_got_service_request; });
}

TEST_F(AgentRunnerTest, AddRunningAgent_IsGracefullyTornDown) {
  fuchsia::testing::modular::TestProtocolPtr service_ptr;
  auto service_ptr_request = service_ptr.NewRequest();

  // Register to intercept the agent launch.
  std::unique_ptr<TestAgent> test_agent;
  launcher()->RegisterComponent(
      kTestAgentUrl, [&](fuchsia::sys::LaunchInfo launch_info,
                         fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        test_agent = std::make_unique<TestAgent>(
            std::move(launch_info.directory_request), std::move(ctrl),
            /*services_ptr=*/nullptr, /*serve_lifecycle_protocol=*/true);
      });

  auto agent_runner = GetOrCreateAgentRunner();
  fuchsia::modular::session::AppConfig agent_app_config;
  agent_app_config.set_url(kTestAgentUrl);
  auto agent_app_client = std::make_unique<modular::AppClient<fuchsia::modular::Lifecycle>>(
      launcher(), std::move(agent_app_config), /* data_origin = */ "");
  agent_runner->AddRunningAgent(kTestAgentUrl, std::move(agent_app_client));

  // Teardown the agent runner.
  auto is_torn_down = false;
  agent_runner->Teardown([&is_torn_down] { is_torn_down = true; });
  RunLoopUntil([&] { return is_torn_down; });

  EXPECT_TRUE(test_agent->lifecycle_terminate_called());
  EXPECT_FALSE(test_agent->controller_connected());
}

TEST_F(AgentRunnerTest, AddRunningAgent_CanBeCriticalAgent) {
  std::unique_ptr<TestAgent> test_agent;
  launcher()->RegisterComponent(
      kTestAgentUrl, [&test_agent](fuchsia::sys::LaunchInfo launch_info,
                                   fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        test_agent =
            std::make_unique<TestAgent>(std::move(launch_info.directory_request), std::move(ctrl));
      });

  // The session should be restarted when the agent terminates.
  set_restart_session_on_agent_crash({kTestAgentUrl});

  auto is_restart_called = false;
  set_on_session_restart_callback([&is_restart_called] { is_restart_called = true; });

  auto agent_runner = GetOrCreateAgentRunner();
  fuchsia::modular::session::AppConfig agent_app_config;
  agent_app_config.set_url(kTestAgentUrl);
  auto agent_app_client = std::make_unique<modular::AppClient<fuchsia::modular::Lifecycle>>(
      launcher(), std::move(agent_app_config), /* data_origin = */ "");
  agent_runner->AddRunningAgent(kTestAgentUrl, std::move(agent_app_client));
  RunLoopUntil([&test_agent] { return !!test_agent; });

  // The agent is now running, so the session should not have been restarted yet.
  EXPECT_FALSE(is_restart_called);

  // Terminate the agent.
  test_agent->KillApplication();

  RunLoopUntil([&is_restart_called] { return is_restart_called; });
}

// Tests that GetAgentOutgoingServices() return null for a non-existent agent, and returns
// a pointer to the component::Services for the agent component when given a valid running
// agent.
TEST_F(AgentRunnerTest, GetAgentOutgoingServices) {
  // Register to intercept the agent launch.
  std::unique_ptr<TestAgent> test_agent;
  int service_connect_requests = 0;
  launcher()->RegisterComponent(
      kTestAgentUrl, [&](fuchsia::sys::LaunchInfo launch_info,
                         fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        test_agent =
            std::make_unique<TestAgent>(std::move(launch_info.directory_request), std::move(ctrl));
        test_agent->AddOutgoingService(
            fidl::InterfaceRequestHandler<fuchsia::testing::modular::TestProtocol>(
                [&](fidl::InterfaceRequest<fuchsia::testing::modular::TestProtocol> request) {
                  ++service_connect_requests;
                }));
      });

  set_agent_service_index({{fuchsia::testing::modular::TestProtocol::Name_, kTestAgentUrl}});

  auto agent_runner = GetOrCreateAgentRunner();
  EXPECT_EQ(nullptr, agent_runner->GetAgentOutgoingServices("noexist"));

  fuchsia::testing::modular::TestProtocolPtr service_ptr1, service_ptr2;

  fuchsia::modular::AgentServiceRequest request;
  request.set_service_name(fuchsia::testing::modular::TestProtocol::Name_);
  request.set_channel(service_ptr1.NewRequest().TakeChannel());
  agent_runner->ConnectToAgentService("requestor_url", std::move(request));

  auto agent_services = agent_runner->GetAgentOutgoingServices(kTestAgentUrl);
  ASSERT_NE(nullptr, agent_services);
  agent_services->ConnectToService(service_ptr2.NewRequest());

  RunLoopUntil([&] { return service_connect_requests == 2; });
}

// Tests that AgentRunner terminates an agent component on teardown. In this case, the agent does
// not serve |fuchsia::modular::Lifecycle| protocol that allows a graceful teardown.
TEST_F(AgentRunnerTest, TerminateOnTeardown) {
  std::unique_ptr<TestAgent> test_agent;
  launcher()->RegisterComponent(
      kTestAgentUrl, [&test_agent](fuchsia::sys::LaunchInfo launch_info,
                                   fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        test_agent =
            std::make_unique<TestAgent>(std::move(launch_info.directory_request), std::move(ctrl));
      });

  fuchsia::sys::ServiceProviderPtr incoming_services;
  fuchsia::modular::AgentControllerPtr agent_controller;
  GetOrCreateAgentRunner()->ConnectToAgent("requestor_url", kTestAgentUrl,
                                           incoming_services.NewRequest(),
                                           agent_controller.NewRequest());

  RunLoopUntil([&test_agent] { return !!test_agent; });

  EXPECT_TRUE(agent_controller.is_bound());

  // Teardown the agent runner.
  auto is_torn_down = false;
  GetOrCreateAgentRunner()->Teardown([&is_torn_down] { is_torn_down = true; });
  RunLoopUntil([&is_torn_down] { return is_torn_down; });

  // The agent should have been terminated.
  EXPECT_FALSE(test_agent->controller_connected());
  // Closing a channel is akin to sending a final message on that channel.
  // Run the run loop until that message is received to see that the
  // AgentController was indeed closed.
  RunLoopUntil([&] { return !agent_controller.is_bound(); });
}

// Tests that AgentRunner terminates an agent component on teardown. In this case, the agent
// serves the |fuchsia::modular::Lifecycle| protocol that allows a graceful teardown.
TEST_F(AgentRunnerTest, TerminateGracefullyOnTeardown) {
  std::unique_ptr<TestAgent> test_agent;
  launcher()->RegisterComponent(
      kTestAgentUrl, [&test_agent](fuchsia::sys::LaunchInfo launch_info,
                                   fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        test_agent = std::make_unique<TestAgent>(
            std::move(launch_info.directory_request), std::move(ctrl),
            /*services_ptr=*/nullptr, /*serve_lifecycle_protocol=*/true);
      });

  fuchsia::sys::ServiceProviderPtr incoming_services;
  fuchsia::modular::AgentControllerPtr agent_controller;
  GetOrCreateAgentRunner()->ConnectToAgent("requestor_url", kTestAgentUrl,
                                           incoming_services.NewRequest(),
                                           agent_controller.NewRequest());

  RunLoopUntil([&] { return !!test_agent; });

  EXPECT_TRUE(agent_controller.is_bound());

  // Teardown the agent runner.
  auto is_torn_down = false;
  GetOrCreateAgentRunner()->Teardown([&is_torn_down] { is_torn_down = true; });
  RunLoopUntil([&] { return is_torn_down; });

  // The agent should have been instructed to tear down gracefully.
  EXPECT_TRUE(test_agent->lifecycle_terminate_called());

  // The agent should have been terminated.
  EXPECT_FALSE(test_agent->controller_connected());
  // Closing a channel is akin to sending a final message on that channel.
  // Run the run loop until that message is received to see that the
  // AgentController was indeed closed.
  RunLoopUntil([&] { return !agent_controller.is_bound(); });
}

// When an agent dies and it is not listed in |restart_session_on_agent_crash|,
// the session should not be restarted.
TEST_F(AgentRunnerTest, CriticalAgents_NoSessionRestartOnCrash) {
  std::unique_ptr<TestAgent> test_agent;
  launcher()->RegisterComponent(
      kTestAgentUrl, [&test_agent](fuchsia::sys::LaunchInfo launch_info,
                                   fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        test_agent =
            std::make_unique<TestAgent>(std::move(launch_info.directory_request), std::move(ctrl));
      });

  // The session should not be restarted due to an agent termination.
  set_restart_session_on_agent_crash({});
  set_on_session_restart_callback(
      [] { FX_NOTREACHED() << "SessionRestartController.Restart() was unexpectedly called"; });

  fuchsia::sys::ServiceProviderPtr incoming_services;
  fuchsia::modular::AgentControllerPtr agent_controller;
  GetOrCreateAgentRunner()->ConnectToAgent("requestor_url", kTestAgentUrl,
                                           incoming_services.NewRequest(),
                                           agent_controller.NewRequest());
  RunLoopUntil([&test_agent] { return !!test_agent; });

  // Terminate the agent.
  test_agent->KillApplication();

  // Teardown the session.
  auto is_torn_down = false;
  GetOrCreateAgentRunner()->Teardown([&is_torn_down] { is_torn_down = true; });
  RunLoopUntil([&is_torn_down] { return is_torn_down; });
}

// When an agent dies and it is listed in |restart_session_on_agent_crash|,
// the session should restarted.
TEST_F(AgentRunnerTest, CriticalAgents_SessionRestartOnCrash) {
  std::unique_ptr<TestAgent> test_agent;
  launcher()->RegisterComponent(
      kTestAgentUrl, [&test_agent](fuchsia::sys::LaunchInfo launch_info,
                                   fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        test_agent =
            std::make_unique<TestAgent>(std::move(launch_info.directory_request), std::move(ctrl));
      });

  // The session should be restarted when the agent terminates.
  set_restart_session_on_agent_crash({kTestAgentUrl});

  auto is_restart_called = false;
  set_on_session_restart_callback([&is_restart_called] { is_restart_called = true; });

  fuchsia::sys::ServiceProviderPtr incoming_services;
  fuchsia::modular::AgentControllerPtr agent_controller;
  GetOrCreateAgentRunner()->ConnectToAgent("requestor_url", kTestAgentUrl,
                                           incoming_services.NewRequest(),
                                           agent_controller.NewRequest());
  RunLoopUntil([&test_agent] { return !!test_agent; });

  // The agent is now running, so the session should not have been restarted yet.
  EXPECT_FALSE(is_restart_called);

  // Terminate the agent.
  test_agent->KillApplication();

  RunLoopUntil([&is_restart_called] { return is_restart_called; });

  // Teardown the session.
  auto is_torn_down = false;
  GetOrCreateAgentRunner()->Teardown([&is_torn_down] { is_torn_down = true; });
  RunLoopUntil([&is_torn_down] { return is_torn_down; });
}

}  // namespace modular_testing
