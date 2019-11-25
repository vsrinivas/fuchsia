// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/entity_provider_runner/entity_provider_runner.h"

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/testing/fake_launcher.h>

#include <memory>

#include <fs/pseudo_dir.h>
#include <fs/service.h>
#include <fs/synchronous_vfs.h>

#include "gtest/gtest.h"
#include "src/lib/component/cpp/connect.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/macros.h"
#include "src/modular/bin/sessionmgr/agent_runner/agent_runner.h"
#include "src/modular/bin/sessionmgr/entity_provider_runner/entity_provider_launcher.h"
#include "src/modular/lib/fidl/array_to_string.h"
#include "src/modular/lib/testing/mock_base.h"

namespace modular_testing {
namespace {

using ::sys::testing::FakeLauncher;

class EntityProviderRunnerTest : public gtest::RealLoopFixture, modular::EntityProviderLauncher {
 public:
  EntityProviderRunnerTest() = default;

  void SetUp() override {
    gtest::RealLoopFixture::SetUp();

    entity_provider_runner_ = std::make_unique<modular::EntityProviderRunner>(
        static_cast<modular::EntityProviderLauncher*>(this));
    // The |fuchsia::modular::UserIntelligenceProvider| below must be nullptr in
    // order for agent creation to be synchronous, which these tests assume.
    agent_runner_ = std::make_unique<modular::AgentRunner>(
        &launcher_, token_manager_.get(), nullptr, entity_provider_runner_.get(), &node_);
  }

  void TearDown() override {
    agent_runner_.reset();
    entity_provider_runner_.reset();

    gtest::RealLoopFixture::TearDown();
  }

 protected:
  modular::AgentRunner* agent_runner() { return agent_runner_.get(); }
  FakeLauncher* launcher() { return &launcher_; }
  modular::EntityProviderRunner* entity_provider_runner() { return entity_provider_runner_.get(); }

 private:
  // TODO(vardhan): A test probably shouldn't be implementing this..
  // |modular::EntityProviderLauncher|
  void ConnectToEntityProvider(
      const std::string& agent_url,
      fidl::InterfaceRequest<fuchsia::modular::EntityProvider> entity_provider_request,
      fidl::InterfaceRequest<fuchsia::modular::AgentController> agent_controller_request) override {
    agent_runner_->ConnectToEntityProvider(agent_url, std::move(entity_provider_request),
                                           std::move(agent_controller_request));
  }

  void ConnectToStoryEntityProvider(
      const std::string& story_id,
      fidl::InterfaceRequest<fuchsia::modular::EntityProvider> entity_provider_request) override {
    FXL_NOTIMPLEMENTED();
  }

  FakeLauncher launcher_;

  inspect::Node node_;

  files::ScopedTempDir mq_data_dir_;
  std::unique_ptr<modular::EntityProviderRunner> entity_provider_runner_;
  std::unique_ptr<modular::AgentRunner> agent_runner_;

  fuchsia::auth::TokenManagerPtr token_manager_;

  FXL_DISALLOW_COPY_AND_ASSIGN(EntityProviderRunnerTest);
};

class MyEntityProvider : fuchsia::modular::Agent,
                         fuchsia::modular::EntityProvider,
                         public fuchsia::sys::ComponentController,
                         public modular_testing::MockBase {
 public:
  MyEntityProvider(fuchsia::sys::LaunchInfo launch_info,
                   fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl)
      : vfs_(async_get_default_dispatcher()),
        outgoing_directory_(fbl::AdoptRef(new fs::PseudoDir())),
        controller_(this, std::move(ctrl)),
        agent_binding_(this),
        entity_provider_binding_(this),
        launch_info_(std::move(launch_info)) {
    outgoing_directory_->AddEntry(fuchsia::modular::Agent::Name_,
                                  fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
                                    agent_binding_.Bind(std::move(channel));
                                    return ZX_OK;
                                  })));
    outgoing_directory_->AddEntry(fuchsia::modular::EntityProvider::Name_,
                                  fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
                                    entity_provider_binding_.Bind(std::move(channel));
                                    return ZX_OK;
                                  })));
    vfs_.ServeDirectory(outgoing_directory_, std::move(launch_info_.directory_request));

    // Get |agent_context_| and |entity_resolver_| from incoming namespace.
    FXL_CHECK(launch_info_.additional_services);
    FXL_CHECK(launch_info_.additional_services->provider.is_valid());
    auto additional_services = launch_info_.additional_services->provider.Bind();
    component::ConnectToService(additional_services.get(), agent_context_.NewRequest());
    fuchsia::modular::ComponentContextPtr component_context;
    agent_context_->GetComponentContext(component_context.NewRequest());
    component_context->GetEntityResolver(entity_resolver_.NewRequest());
  }

  size_t GetCallCount(const std::string func) { return counts.count(func); }
  fuchsia::modular::EntityResolver* entity_resolver() { return entity_resolver_.get(); }
  fuchsia::modular::AgentContext* agent_context() { return agent_context_.get(); }

 private:
  // |ComponentController|
  void Kill() override { ++counts["Kill"]; }
  // |ComponentController|
  void Detach() override { ++counts["Detach"]; }

  // |fuchsia::modular::Agent|
  void Connect(std::string requestor_url,
               fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> outgoing_services) override {
    ++counts["Connect"];
  }

  // |fuchsia::modular::EntityProvider|
  void GetTypes(std::string cookie, GetTypesCallback callback) override {
    std::vector<std::string> types;
    types.push_back("MyType");
    callback(std::move(types));
  }

  // |fuchsia::modular::EntityProvider|
  void GetData(std::string cookie, std::string type, GetDataCallback callback) override {
    fsl::SizedVmo vmo;
    FXL_CHECK(fsl::VmoFromString(type + ":MyData", &vmo));
    auto vmo_ptr = std::make_unique<fuchsia::mem::Buffer>(std::move(vmo).ToTransport());

    callback(std::move(vmo_ptr));
  }

  // |fuchsia::modular::EntityProvider|
  void WriteData(std::string cookie, std::string type, fuchsia::mem::Buffer data,
                 WriteDataCallback callback) override {
    // TODO(MI4-1301)
    callback(fuchsia::modular::EntityWriteStatus::READ_ONLY);
  }

  // |fuchsia::modular::EntityProvider|
  void Watch(std::string cookie, std::string type,
             fidl::InterfaceHandle<fuchsia::modular::EntityWatcher> watcher) override {
    // TODO(MI4-1301)
    FXL_NOTIMPLEMENTED();
  }

 private:
  fs::SynchronousVfs vfs_;
  fbl::RefPtr<fs::PseudoDir> outgoing_directory_;
  fuchsia::modular::AgentContextPtr agent_context_;
  fuchsia::modular::EntityResolverPtr entity_resolver_;
  fidl::Binding<fuchsia::sys::ComponentController> controller_;
  fidl::Binding<fuchsia::modular::Agent> agent_binding_;
  fidl::Binding<fuchsia::modular::EntityProvider> entity_provider_binding_;
  fuchsia::sys::LaunchInfo launch_info_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MyEntityProvider);
};

TEST_F(EntityProviderRunnerTest, Basic) {
  std::unique_ptr<MyEntityProvider> dummy_agent;
  constexpr char kMyAgentUrl[] = "file:///my_agent";
  launcher()->RegisterComponent(
      kMyAgentUrl, [&dummy_agent](fuchsia::sys::LaunchInfo launch_info,
                                  fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        dummy_agent = std::make_unique<MyEntityProvider>(std::move(launch_info), std::move(ctrl));
      });

  // 1. Start up the entity provider agent.
  fuchsia::sys::ServiceProviderPtr incoming_services;
  fuchsia::modular::AgentControllerPtr agent_controller;
  agent_runner()->ConnectToAgent("dummy_requestor_url", kMyAgentUrl, incoming_services.NewRequest(),
                                 agent_controller.NewRequest());

  RunLoopWithTimeoutOrUntil([&dummy_agent] {
    return dummy_agent.get() != nullptr && dummy_agent->GetCallCount("Connect") == 1;
  });
  dummy_agent->ExpectCalledOnce("Connect");

  // 2. Make an entity reference on behalf of this agent.
  // The framework should use |kMyAgentUrl| as the agent to associate new
  // references.
  fuchsia::modular::EntityReferenceFactoryPtr factory;
  dummy_agent->agent_context()->GetEntityReferenceFactory(factory.NewRequest());
  fidl::StringPtr entity_ref;
  factory->CreateReference("my_cookie",
                           [&entity_ref](fidl::StringPtr retval) { entity_ref = retval; });

  RunLoopWithTimeoutOrUntil([&entity_ref] { return entity_ref.has_value(); });
  EXPECT_TRUE(entity_ref.has_value());

  // 3. Resolve the reference into an |fuchsia::modular::Entity|, make calls to
  // GetTypes and
  //    GetData, which should route into our |MyEntityProvider|.
  fuchsia::modular::EntityPtr entity;
  dummy_agent->entity_resolver()->ResolveEntity(entity_ref.value(), entity.NewRequest());

  std::map<std::string, uint32_t> counts;
  entity->GetTypes([&counts](const std::vector<std::string>& types) {
    EXPECT_EQ(1u, types.size());
    EXPECT_EQ("MyType", types.at(0));
    counts["GetTypes"]++;
  });
  entity->GetData("MyType", [&counts](std::unique_ptr<fuchsia::mem::Buffer> data) {
    std::string data_string;
    FXL_CHECK(fsl::StringFromVmo(*data, &data_string));
    EXPECT_EQ("MyType:MyData", data_string);
    counts["GetData"]++;
  });
  RunLoopWithTimeoutOrUntil(
      [&counts] { return counts["GetTypes"] == 1 && counts["GetData"] == 1; });
  EXPECT_EQ(1u, counts["GetTypes"]);
  EXPECT_EQ(1u, counts["GetData"]);
}

}  // namespace
}  // namespace modular_testing
