// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/agent_runner/agent_runner.h"

#include <fs/service.h>
#include <fs/synchronous-vfs.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/component/cpp/service_provider_impl.h>
#include <lib/component/cpp/testing/fake_launcher.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/zx/object.h>
#include <src/lib/fxl/macros.h>
#include <zircon/status.h>

#include <memory>

#include "gtest/gtest.h"
#include "peridot/bin/sessionmgr/agent_runner/map_agent_service_index.h"
#include "peridot/bin/sessionmgr/entity_provider_runner/entity_provider_runner.h"
#include "peridot/bin/sessionmgr/message_queue/message_queue_manager.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/ledger_client/page_id.h"
#include "peridot/lib/testing/fake_agent_runner_storage.h"
#include "peridot/lib/testing/mock_base.h"
#include "peridot/lib/testing/test_with_ledger.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace modular {
namespace testing {
namespace {

using ::component::testing::FakeLauncher;

class AgentRunnerTest : public TestWithLedger {
 public:
  AgentRunnerTest() = default;

  std::unique_ptr<AgentRunner> MakeAgentRunner(
      std::unique_ptr<AgentServiceIndex> custom_index = nullptr) {
    return std::make_unique<AgentRunner>(
        &launcher_, mqm_.get(), ledger_repository(), &agent_runner_storage_,
        token_manager_.get(), nullptr, entity_provider_runner_.get(),
        std::move(custom_index));
  }

  void SetUp() override {
    TestWithLedger::SetUp();

    mqm_ = std::make_unique<MessageQueueManager>(
        ledger_client(), MakePageId("0123456789123456"), mq_data_dir_.path());
    entity_provider_runner_ = std::make_unique<EntityProviderRunner>(nullptr);
    // The |fuchsia::modular::UserIntelligenceProvider| below must be nullptr in
    // order for agent creation to be synchronous, which these tests assume.
  }

  void TearDown() override {
    agent_runner_.reset();
    entity_provider_runner_.reset();
    mqm_.reset();

    TestWithLedger::TearDown();
  }

  MessageQueueManager* message_queue_manager() { return mqm_.get(); }

 protected:
  void set_agent_runner(std::unique_ptr<AgentRunner> agent_runner) {
    agent_runner_ = std::move(agent_runner);
  }

  AgentRunner* agent_runner() {
    if (agent_runner_ == nullptr) {
      set_agent_runner(MakeAgentRunner());
    }
    return agent_runner_.get();
  }

  void set_service_to_agent_map(
      std::map<std::string, std::string> service_name_to_agent_url) {
    set_agent_runner(MakeAgentRunner(
        std::make_unique<MapAgentServiceIndex>(service_name_to_agent_url)));
  }

  template <typename Interface>
  void request_agent_service(
      std::string service_name,
      fidl::InterfaceRequest<Interface> service_request,
      fuchsia::modular::AgentControllerPtr agent_controller) {
    fuchsia::modular::AgentServiceRequest agent_service_request;
    agent_service_request.set_service_name(service_name);
    agent_service_request.set_channel(service_request.TakeChannel());
    agent_service_request.set_agent_controller(agent_controller.NewRequest());
    agent_runner()->ConnectToAgentService("requestor_url",
                                          std::move(agent_service_request));
  }

  FakeLauncher* launcher() { return &launcher_; }

 private:
  FakeLauncher launcher_;

  files::ScopedTempDir mq_data_dir_;
  std::unique_ptr<MessageQueueManager> mqm_;
  FakeAgentRunnerStorage agent_runner_storage_;
  std::unique_ptr<EntityProviderRunner> entity_provider_runner_;
  std::unique_ptr<AgentRunner> agent_runner_;

  fuchsia::auth::TokenManagerPtr token_manager_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AgentRunnerTest);
};

class TestAgent : fuchsia::modular::Agent,
                  public fuchsia::sys::ComponentController,
                  public testing::MockBase {
 public:
  TestAgent(zx::channel directory_request,
            fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl,
            std::unique_ptr<component::ServiceNamespace> services_ptr = nullptr)
      : vfs_(async_get_default_dispatcher()),
        outgoing_directory_(fbl::AdoptRef(new fs::PseudoDir())),
        controller_(this, std::move(ctrl)),
        agent_binding_(this),
        services_ptr_(std::move(services_ptr)) {
    outgoing_directory_->AddEntry(
        fuchsia::modular::Agent::Name_,
        fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
          agent_binding_.Bind(std::move(channel));
          return ZX_OK;
        })));
    vfs_.ServeDirectory(outgoing_directory_, std::move(directory_request));
  }

  void KillApplication() { controller_.Unbind(); }

  size_t GetCallCount(const std::string func) { return counts.count(func); }

 private:
  // |ComponentController|
  void Kill() override { ++counts["Kill"]; }
  // |ComponentController|
  void Detach() override { ++counts["Detach"]; }

  // |fuchsia::modular::Agent|
  void Connect(std::string /*requestor_url*/,
               fidl::InterfaceRequest<fuchsia::sys::ServiceProvider>
                   outgoing_services) override {
    ++counts["Connect"];
    if (services_ptr_) {
      services_ptr_->AddBinding(std::move(outgoing_services));
    }
  }

  // |fuchsia::modular::Agent|
  void RunTask(std::string /*task_id*/, RunTaskCallback /*callback*/) override {
    ++counts["RunTask"];
  }

 private:
  fs::SynchronousVfs vfs_;
  fbl::RefPtr<fs::PseudoDir> outgoing_directory_;
  fidl::Binding<fuchsia::sys::ComponentController> controller_;
  fidl::Binding<fuchsia::modular::Agent> agent_binding_;

  std::unique_ptr<component::ServiceNamespace> services_ptr_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestAgent);
};

}  // namespace

// Test that connecting to an agent will start it up.
// Then there should be an fuchsia::modular::Agent.Connect().
TEST_F(AgentRunnerTest, ConnectToAgent) {
  int agent_launch_count = 0;
  std::unique_ptr<TestAgent> test_agent;
  constexpr char kMyAgentUrl[] = "file:///my_agent";
  launcher()->RegisterComponent(
      kMyAgentUrl,
      [&test_agent, &agent_launch_count](
          fuchsia::sys::LaunchInfo launch_info,
          fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        test_agent = std::make_unique<TestAgent>(
            std::move(launch_info.directory_request), std::move(ctrl));
        ++agent_launch_count;
      });

  fuchsia::sys::ServiceProviderPtr incoming_services;
  fuchsia::modular::AgentControllerPtr agent_controller;
  agent_runner()->ConnectToAgent("requestor_url", kMyAgentUrl,
                                 incoming_services.NewRequest(),
                                 agent_controller.NewRequest());

  RunLoopWithTimeoutOrUntil([&test_agent] {
    return test_agent && test_agent->GetCallCount("Connect") > 0;
  });
  EXPECT_EQ(1, agent_launch_count);
  test_agent->ExpectCalledOnce("Connect");
  test_agent->ExpectNoOtherCalls();

  // Connecting to the same agent again shouldn't launch a new instance and
  // shouldn't re-initialize the existing instance of the agent application,
  // but should call |Connect()|.

  fuchsia::modular::AgentControllerPtr agent_controller2;
  fuchsia::sys::ServiceProviderPtr incoming_services2;
  agent_runner()->ConnectToAgent("requestor_url2", kMyAgentUrl,
                                 incoming_services2.NewRequest(),
                                 agent_controller2.NewRequest());

  RunLoopWithTimeoutOrUntil([&test_agent] {
    return test_agent && test_agent->GetCallCount("Connect");
  });
  EXPECT_EQ(1, agent_launch_count);
  test_agent->ExpectCalledOnce("Connect");
  test_agent->ExpectNoOtherCalls();
}

// Test that if an agent application dies, it is removed from agent runner
// (which means outstanding AgentControllers are closed).
TEST_F(AgentRunnerTest, AgentController) {
  std::unique_ptr<TestAgent> test_agent;
  constexpr char kMyAgentUrl[] = "file:///my_agent";
  launcher()->RegisterComponent(
      kMyAgentUrl,
      [&test_agent](
          fuchsia::sys::LaunchInfo launch_info,
          fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        test_agent = std::make_unique<TestAgent>(
            std::move(launch_info.directory_request), std::move(ctrl));
      });

  fuchsia::sys::ServiceProviderPtr incoming_services;
  fuchsia::modular::AgentControllerPtr agent_controller;
  agent_runner()->ConnectToAgent("requestor_url", kMyAgentUrl,
                                 incoming_services.NewRequest(),
                                 agent_controller.NewRequest());

  RunLoopWithTimeoutOrUntil([&test_agent] { return !!test_agent; });
  test_agent->KillApplication();

  // fuchsia::modular::Agent application died, so check that
  // fuchsia::modular::AgentController dies here.
  agent_controller.set_error_handler(
      [&agent_controller](zx_status_t status) { agent_controller.Unbind(); });
  RunLoopWithTimeoutOrUntil(
      [&agent_controller] { return !agent_controller.is_bound(); });
  EXPECT_FALSE(agent_controller.is_bound());
}

TEST_F(AgentRunnerTest, NoServiceNameInAgentServiceRequest) {
  std::unique_ptr<TestAgent> test_agent;
  constexpr char kMyAgentUrl[] = "file:///my_agent";
  launcher()->RegisterComponent(
      kMyAgentUrl,
      [&test_agent](
          fuchsia::sys::LaunchInfo launch_info,
          fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        test_agent = std::make_unique<TestAgent>(
            std::move(launch_info.directory_request), std::move(ctrl));
      });

  bool service_error = false;

  // We use an InterfacePtr<Clipboard> to take advantage of set_error_handler().
  // The choice of Clipboard is arbitrary: InterfacePtr requires a FIDL protocol
  // type name, but the choice of FIDL protocol is irrelevant for this test.
  fuchsia::modular::ClipboardPtr service_ptr;
  fuchsia::modular::AgentControllerPtr agent_controller;
  service_ptr.set_error_handler([&service_error](zx_status_t status) {
    service_error = true;
    EXPECT_EQ(status, ZX_ERR_PEER_CLOSED);
  });
  fuchsia::modular::AgentServiceRequest agent_service_request;
  // agent_service_request.set_service_name(service_ptr->Name_);
  agent_service_request.set_channel(service_ptr.NewRequest().TakeChannel());
  agent_service_request.set_agent_controller(agent_controller.NewRequest());
  agent_runner()->ConnectToAgentService("requestor_url",
                                        std::move(agent_service_request));

  bool agent_controller_error = false;

  agent_controller.set_error_handler(
      [&agent_controller_error, &agent_controller](zx_status_t status) {
        agent_controller_error = true;
        EXPECT_EQ(status, ZX_ERR_PEER_CLOSED);
      });

  RunLoopWithTimeoutOrUntil([&service_error, &agent_controller_error] {
    return service_error || agent_controller_error;
  });

  EXPECT_TRUE(service_error);
  EXPECT_TRUE(agent_controller_error);

  EXPECT_EQ(test_agent, nullptr);
}

TEST_F(AgentRunnerTest, NoChannelInAgentServiceRequest) {
  std::unique_ptr<TestAgent> test_agent;
  constexpr char kMyAgentUrl[] = "file:///my_agent";
  launcher()->RegisterComponent(
      kMyAgentUrl,
      [&test_agent](
          fuchsia::sys::LaunchInfo launch_info,
          fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        test_agent = std::make_unique<TestAgent>(
            std::move(launch_info.directory_request), std::move(ctrl));
      });

  bool service_error = false;

  fuchsia::modular::ClipboardPtr service_ptr;
  fuchsia::modular::AgentControllerPtr agent_controller;
  service_ptr.set_error_handler([&service_error](zx_status_t status) {
    service_error = true;
    EXPECT_EQ(status, ZX_ERR_PEER_CLOSED);
  });
  fuchsia::modular::AgentServiceRequest agent_service_request;
  agent_service_request.set_service_name(service_ptr->Name_);
  // agent_service_request.set_channel(service_ptr.NewRequest().TakeChannel());
  agent_service_request.set_agent_controller(agent_controller.NewRequest());
  agent_runner()->ConnectToAgentService("requestor_url",
                                        std::move(agent_service_request));

  bool agent_controller_error = false;

  agent_controller.set_error_handler(
      [&agent_controller_error, &agent_controller](zx_status_t status) {
        agent_controller_error = true;
        EXPECT_EQ(status, ZX_ERR_PEER_CLOSED);
      });

  RunLoopWithTimeoutOrUntil([&service_error, &agent_controller_error] {
    return service_error || agent_controller_error;
  });

  EXPECT_FALSE(service_error);
  EXPECT_TRUE(agent_controller_error);

  EXPECT_EQ(test_agent, nullptr);
}

TEST_F(AgentRunnerTest, NoAgentForServiceName) {
  constexpr char kMyAgentUrl[] = "file:///my_agent";

  // Client-side service pointer
  fuchsia::modular::ClipboardPtr service_ptr;
  auto service_name = service_ptr->Name_;
  auto service_request = service_ptr.NewRequest();
  bool service_error = false;
  service_ptr.set_error_handler([&service_error](zx_status_t status) {
    service_error = true;
    EXPECT_EQ(status, ZX_ERR_NOT_FOUND);
  });

  // requested service will not have a matching agent
  set_service_to_agent_map(std::map<std::string, std::string>({}));

  // standard AgentController initialization
  fuchsia::modular::AgentControllerPtr agent_controller;
  bool agent_controller_error = false;
  agent_controller.set_error_handler(
      [&agent_controller_error, &agent_controller](zx_status_t status) {
        agent_controller_error = true;
        EXPECT_EQ(status, ZX_ERR_PEER_CLOSED);
      });

  // register and launch the test agent, WITHOUT services
  std::unique_ptr<TestAgent> test_agent;
  launcher()->RegisterComponent(
      kMyAgentUrl,
      [&test_agent](
          fuchsia::sys::LaunchInfo launch_info,
          fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        test_agent = std::make_unique<TestAgent>(
            std::move(launch_info.directory_request), std::move(ctrl));
      });

  request_agent_service(service_name, std::move(service_request),
                        std::move(agent_controller));

  RunLoopWithTimeoutOrUntil([&service_error, &agent_controller_error] {
    return service_error || agent_controller_error;
  });

  EXPECT_TRUE(service_error);
  EXPECT_FALSE(agent_controller_error);
}

static zx_koid_t get_object_koid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  if (zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info),
                         NULL, NULL) != ZX_OK) {
    return 0;
  }
  return info.koid;
}

TEST_F(AgentRunnerTest, ConnectToServiceName) {
  constexpr char kMyAgentUrl[] = "file:///my_agent";

  // Client-side service pointer
  fuchsia::modular::ClipboardPtr service_ptr;
  auto service_name = service_ptr->Name_;
  auto service_request = service_ptr.NewRequest();
  bool service_error = false;
  service_ptr.set_error_handler([&service_error](zx_status_t status) {
    service_error = true;
    // In this test, the agent does not complete the connection, so we expect
    // a PEER_CLOSED error.
    EXPECT_EQ(status, ZX_ERR_PEER_CLOSED);
  });

  // requested service will map to test agent
  set_service_to_agent_map(std::map<std::string, std::string>({
      {service_name, kMyAgentUrl},
  }));

  // standard AgentController initialization
  fuchsia::modular::AgentControllerPtr agent_controller;
  bool agent_controller_error = false;
  agent_controller.set_error_handler(
      [&agent_controller_error, &agent_controller](zx_status_t status) {
        agent_controller_error = true;
        EXPECT_EQ(status, ZX_ERR_PEER_CLOSED);
      });

  // register a service for the agent to serve, and expect the client's request
  auto services_ptr = std::make_unique<component::ServiceNamespace>();
  bool agent_got_service_request = false;
  services_ptr->AddService<fuchsia::modular::Clipboard>(
      [&agent_got_service_request,
       client_request_koid = get_object_koid(service_request.channel().get())](
          fidl::InterfaceRequest<fuchsia::modular::Clipboard> request) {
        auto server_request_koid = get_object_koid(request.channel().get());
        EXPECT_EQ(server_request_koid, client_request_koid);
        agent_got_service_request = true;
      });

  // register and launch the test agent, with services
  std::unique_ptr<TestAgent> test_agent;
  launcher()->RegisterComponent(
      kMyAgentUrl,
      [&test_agent, &services_ptr](
          fuchsia::sys::LaunchInfo launch_info,
          fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        test_agent = std::make_unique<TestAgent>(
            std::move(launch_info.directory_request), std::move(ctrl),
            std::move(services_ptr));
      });

  request_agent_service(service_name, std::move(service_request),
                        std::move(agent_controller));

  RunLoopWithTimeoutOrUntil([&agent_got_service_request, &service_error,
                             &agent_controller_error] {
    return agent_got_service_request || service_error || agent_controller_error;
  });

  EXPECT_TRUE(agent_got_service_request);
  EXPECT_TRUE(service_error);  // test does not complete connection (see above)
  EXPECT_FALSE(agent_controller_error);
}

}  // namespace testing
}  // namespace modular
