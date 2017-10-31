// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "gtest/gtest.h"
#include "lib/agent/cpp/agent_impl.h"
#include "lib/agent/fidl/agent.fidl.h"
#include "lib/app/cpp/service_provider_impl.h"
#include "lib/app/fidl/service_provider.fidl.h"
#include "lib/auth/fidl/account_provider.fidl.h"
#include "lib/entity/fidl/entity.fidl.h"
#include "lib/entity/fidl/entity_provider.fidl.h"
#include "lib/entity/fidl/entity_reference_factory.fidl.h"
#include "lib/entity/fidl/entity_resolver.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/macros.h"
#include "lib/user_intelligence/fidl/user_intelligence_provider.fidl.h"
#include "peridot/bin/agent_runner/agent_runner.h"
#include "peridot/bin/component/message_queue_manager.h"
#include "peridot/bin/entity/entity_provider_launcher.h"
#include "peridot/bin/entity/entity_provider_runner.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/testing/fake_agent_runner_storage.h"
#include "peridot/lib/testing/fake_application_launcher.h"
#include "peridot/lib/testing/mock_base.h"
#include "peridot/lib/testing/test_with_ledger.h"

namespace modular {
namespace testing {
namespace {

class EntityProviderRunnerTest : public TestWithLedger, EntityProviderLauncher {
 public:
  EntityProviderRunnerTest() = default;

  void SetUp() override {
    TestWithLedger::SetUp();

    mqm_.reset(new MessageQueueManager(
        ledger_client(), to_array("0123456789123456"), "/tmp/test_mq_data"));
    entity_provider_runner_.reset(
        new EntityProviderRunner(static_cast<EntityProviderLauncher*>(this)));
    agent_runner_.reset(
        new AgentRunner(&launcher_, mqm_.get(), ledger_repository(),
                        &agent_runner_storage_, token_provider_factory_.get(),
                        ui_provider_.get(), entity_provider_runner_.get()));
  }

  void TearDown() override {
    agent_runner_.reset();
    entity_provider_runner_.reset();
    mqm_.reset();

    TestWithLedger::TearDown();
  }

  MessageQueueManager* message_queue_manager() { return mqm_.get(); }

 protected:
  AgentRunner* agent_runner() { return agent_runner_.get(); }
  FakeApplicationLauncher* launcher() { return &launcher_; }

 private:
  // TODO(vardhan): A test probably shouldn't be implementing this..
  // |EntityProviderLauncher|
  void ConnectToEntityProvider(
      const std::string& agent_url,
      fidl::InterfaceRequest<EntityProvider> entity_provider_request,
      fidl::InterfaceRequest<AgentController> agent_controller_request)
      override {
    agent_runner_->ConnectToEntityProvider(agent_url,
                                           std::move(entity_provider_request),
                                           std::move(agent_controller_request));
  }

  FakeApplicationLauncher launcher_;

  std::unique_ptr<MessageQueueManager> mqm_;
  FakeAgentRunnerStorage agent_runner_storage_;
  std::unique_ptr<EntityProviderRunner> entity_provider_runner_;
  std::unique_ptr<AgentRunner> agent_runner_;

  auth::TokenProviderFactoryPtr token_provider_factory_;
  maxwell::UserIntelligenceProviderPtr ui_provider_;

  FXL_DISALLOW_COPY_AND_ASSIGN(EntityProviderRunnerTest);
};

class MyEntityProvider : AgentImpl::Delegate,
                         EntityProvider,
                         public app::ApplicationController,
                         public testing::MockBase {
 public:
  MyEntityProvider(app::ApplicationLaunchInfoPtr launch_info,
                   fidl::InterfaceRequest<app::ApplicationController> ctrl)
      : app_controller_(this, std::move(ctrl)),
        entity_provider_binding_(this),
        launch_info_(std::move(launch_info)) {
    outgoing_services_.AddService<EntityProvider>(
        [this](fidl::InterfaceRequest<EntityProvider> request) {
          entity_provider_binding_.Bind(std::move(request));
        });
    outgoing_services_.AddBinding(std::move(launch_info_->services));
    outgoing_services_.ServeDirectory(std::move(launch_info_->service_request));
    agent_impl_ = std::make_unique<AgentImpl>(
        &outgoing_services_, static_cast<AgentImpl::Delegate*>(this));
  }

  size_t GetCallCount(const std::string func) { return counts.count(func); }
  EntityResolver* entity_resolver() { return entity_resolver_.get(); }
  AgentContext* agent_context() { return agent_context_.get(); }

 private:
  // |ApplicationController|
  void Kill() override { ++counts["Kill"]; }
  // |ApplicationController|
  void Detach() override { ++counts["Detach"]; }
  // |ApplicationController|
  void Wait(const WaitCallback& callback) override { ++counts["Wait"]; }

  // |AgentImpl::Delegate|
  void AgentInit(
      fidl::InterfaceHandle<AgentContext> agent_context_handle) override {
    agent_context_.Bind(std::move(agent_context_handle));
    ComponentContextPtr component_context;
    agent_context_->GetComponentContext(component_context.NewRequest());
    component_context->GetEntityResolver(entity_resolver_.NewRequest());
    ++counts["AgentInit"];
  }

  // |AgentImpl::Delegate|
  void Connect(
      fidl::InterfaceRequest<app::ServiceProvider> outgoing_services) override {
    ++counts["Connect"];
  }
  // |AgentImpl::Delegate|
  void RunTask(const fidl::String& task_id,
               const std::function<void()>& done) override {
    ++counts["RunTask"];
    done();
  }

  // |EntityProvider|
  void GetTypes(const fidl::String& cookie,
                const GetTypesCallback& callback) override {
    callback(
        fidl::Array<fidl::String>::From(std::vector<std::string>{"MyType"}));
  }

  // |EntityProvider|
  void GetData(const fidl::String& cookie,
               const fidl::String& type,
               const GetDataCallback& callback) override {
    callback(type.get() + ":MyData");
  }

 private:
  app::ServiceNamespace outgoing_services_;
  AgentContextPtr agent_context_;
  std::unique_ptr<AgentImpl> agent_impl_;
  // only valid after AgentInit() is called.
  EntityResolverPtr entity_resolver_;
  fidl::Binding<app::ApplicationController> app_controller_;
  fidl::Binding<modular::EntityProvider> entity_provider_binding_;
  app::ApplicationLaunchInfoPtr launch_info_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MyEntityProvider);
};

TEST_F(EntityProviderRunnerTest, Basic) {
  std::unique_ptr<MyEntityProvider> dummy_agent;
  constexpr char kMyAgentUrl[] = "file:///my_agent";
  launcher()->RegisterApplication(
      kMyAgentUrl,
      [&dummy_agent](app::ApplicationLaunchInfoPtr launch_info,
                     fidl::InterfaceRequest<app::ApplicationController> ctrl) {
        dummy_agent = std::make_unique<MyEntityProvider>(std::move(launch_info),
                                                         std::move(ctrl));
      });

  // 1. Start up the entity provider agent.
  app::ServiceProviderPtr incoming_services;
  AgentControllerPtr agent_controller;
  agent_runner()->ConnectToAgent("dummy_requestor_url", kMyAgentUrl,
                                 incoming_services.NewRequest(),
                                 agent_controller.NewRequest());

  RunLoopUntil([&dummy_agent] {
    return dummy_agent.get() != nullptr &&
           dummy_agent->GetCallCount("AgentInit") == 1 &&
           dummy_agent->GetCallCount("Connect") == 1;
  });
  dummy_agent->ExpectCalledOnce("AgentInit");
  dummy_agent->ExpectCalledOnce("Connect");

  // 2. Make an entity reference on behalf of this agent.
  // The framework should use |kMyAgentUrl| as the agent to associate new
  // references.
  EntityReferenceFactoryPtr factory;
  dummy_agent->agent_context()->GetEntityReferenceFactory(factory.NewRequest());
  fidl::String entity_ref;
  factory->CreateReference(
      "my_cookie",
      [&entity_ref](const fidl::String& retval) { entity_ref = retval; });

  RunLoopUntil([&entity_ref] { return !entity_ref.is_null(); });
  EXPECT_FALSE(entity_ref.is_null());

  // 3. Resolve the reference into an |Entity|, make calls to GetTypes and
  //    GetData, which should route into our |MyEntityProvider|.
  EntityPtr entity;
  dummy_agent->entity_resolver()->ResolveEntity(entity_ref,
                                                entity.NewRequest());

  std::map<std::string, uint32_t> counts;
  entity->GetTypes([&counts](const fidl::Array<fidl::String>& types) {
    EXPECT_EQ(1u, types.size());
    EXPECT_EQ("MyType", types[0]);
    counts["GetTypes"]++;
  });
  entity->GetData("MyType", [&counts](const fidl::String& data) {
    EXPECT_EQ("MyType:MyData", data.get());
    counts["GetData"]++;
  });
  RunLoopUntil(
      [&counts] { return counts["GetTypes"] == 1 && counts["GetData"] == 1; });
  EXPECT_EQ(1u, counts["GetTypes"]);
  EXPECT_EQ(1u, counts["GetData"]);
}

}  // namespace
}  // namespace testing
}  // namespace modular
