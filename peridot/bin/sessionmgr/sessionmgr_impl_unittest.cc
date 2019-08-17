// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/sessionmgr_impl.h"

#include <lib/component/cpp/connect.h>
#include <lib/component/cpp/testing/fake_launcher.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/outgoing_directory.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/sys/cpp/testing/fake_component.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

// GMock is only used for its testing matchers.
#include "fuchsia/ledger/internal/cpp/fidl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fit/function.h"
#include "lib/ui/scenic/cpp/view_token_pair.h"
#include "lib/vfs/cpp/pseudo_dir.h"
#include "peridot/lib/fidl/clone.h"
#include "peridot/lib/ledger_client/constants.h"
#include "src/lib/fxl/logging.h"

namespace modular {
namespace testing {
namespace {
using ::testing::Eq;
using ::testing::SizeIs;

using SessionmgrImplTest = gtest::TestLoopFixture;

constexpr char kProfileId[] = "profile-id";

// Fake TokenManager for testing. It answers to |ListProfileId| with a single profile.
class FakeTokenManager : public fuchsia::auth::TokenManager {
 public:
  explicit FakeTokenManager(fidl::InterfaceRequest<fuchsia::auth::TokenManager> request)
      : binding_(this, std::move(request)) {}

  ~FakeTokenManager() override {}

  // fuchsia::auth::TokenManager:
  void Authorize(fuchsia::auth::AppConfig app_config,
                 fidl::InterfaceHandle<fuchsia::auth::AuthenticationUIContext> auth_ui_context,
                 std::vector<std::string> app_scopes, fidl::StringPtr user_profile_id,
                 fidl::StringPtr auth_code, AuthorizeCallback callback) override {
    FXL_NOTIMPLEMENTED();
    callback(fuchsia::auth::Status::INTERNAL_ERROR, nullptr);
  }

  void GetAccessToken(fuchsia::auth::AppConfig app_config, std::string user_profile_id,
                      std::vector<std::string> app_scopes,
                      GetAccessTokenCallback callback) override {
    FXL_NOTIMPLEMENTED();
    callback(fuchsia::auth::Status::INTERNAL_ERROR, nullptr);
  }

  void GetIdToken(fuchsia::auth::AppConfig app_config, std::string user_profile_id,
                  fidl::StringPtr audience, GetIdTokenCallback callback) override {
    FXL_NOTIMPLEMENTED();
    callback(fuchsia::auth::Status::INTERNAL_ERROR, nullptr);
  }

  void GetFirebaseToken(fuchsia::auth::AppConfig app_config, std::string user_profile_id,
                        std::string audience, std::string firebase_api_key,
                        GetFirebaseTokenCallback callback) override {
    FXL_NOTIMPLEMENTED();
    callback(fuchsia::auth::Status::INTERNAL_ERROR, nullptr);
  }

  void DeleteAllTokens(fuchsia::auth::AppConfig app_config, std::string user_profile_id, bool force,
                       DeleteAllTokensCallback callback) override {
    FXL_NOTIMPLEMENTED();
    callback(fuchsia::auth::Status::INTERNAL_ERROR);
  }

  void ListProfileIds(fuchsia::auth::AppConfig app_config,
                      ListProfileIdsCallback callback) override {
    callback(fuchsia::auth::Status::OK, {kProfileId});
  }

 private:
  fidl::Binding<fuchsia::auth::TokenManager> binding_;
};

// Fake ComponentController used to connect back to services provided by the launcher.
class FakeComponentController : public fuchsia::sys::ComponentController {
 public:
  FakeComponentController(fuchsia::sys::LaunchInfo launch_info,
                          fidl::InterfaceRequest<fuchsia::sys::ComponentController> request)
      : binding_(this, std::move(request)) {
    if (launch_info.additional_services) {
      services_ = launch_info.additional_services->provider.Bind();
    }
  }

  ~FakeComponentController() override {}

  void Kill() override { FXL_NOTIMPLEMENTED(); }

  void Detach() override { FXL_NOTIMPLEMENTED(); }

  template <typename Interface>
  fidl::InterfacePtr<Interface> Connect() const {
    return component::ConnectToService<Interface>(services_.get());
  }

 private:
  fuchsia::sys::ServiceProviderPtr services_;
  fidl::Binding<fuchsia::sys::ComponentController> binding_;
};

// Fake CloudProviderFactory that records calls to |GetCloudProvider|.
class FakeCloudProviderFactory : public fuchsia::ledger::cloud::firestore::Factory {
 public:
  struct GetCloudProviderRequest {
    fuchsia::ledger::cloud::firestore::Config config;
    fidl::InterfaceHandle<fuchsia::auth::TokenManager> token_manager;
    fidl::InterfaceRequest<fuchsia::ledger::cloud::CloudProvider> cloud_provider;
    fit::function<void(fuchsia::ledger::cloud::Status)> callback;

    GetCloudProviderRequest(
        fuchsia::ledger::cloud::firestore::Config config,
        fidl::InterfaceHandle<fuchsia::auth::TokenManager> token_manager,
        fidl::InterfaceRequest<fuchsia::ledger::cloud::CloudProvider> cloud_provider,
        fit::function<void(fuchsia::ledger::cloud::Status)> callback)
        : config(std::move(config)),
          token_manager(std::move(token_manager)),
          cloud_provider(std::move(cloud_provider)),
          callback(std::move(callback)) {}
  };

  explicit FakeCloudProviderFactory(
      fidl::InterfaceRequest<fuchsia::ledger::cloud::firestore::Factory> request)
      : binding_(this, std::move(request)) {}

  void GetCloudProvider(
      fuchsia::ledger::cloud::firestore::Config config,
      fidl::InterfaceHandle<fuchsia::auth::TokenManager> token_manager,
      fidl::InterfaceRequest<fuchsia::ledger::cloud::CloudProvider> cloud_provider,
      fit::function<void(fuchsia::ledger::cloud::Status)> callback) override {
    get_cloud_provider_requests.emplace_back(std::move(config), std::move(token_manager),
                                             std::move(cloud_provider), std::move(callback));
  }

  std::vector<GetCloudProviderRequest> get_cloud_provider_requests;

 private:
  fidl::Binding<fuchsia::ledger::cloud::firestore::Factory> binding_;
};

// Fake LedgerRepositoryFactory that records calls to |GetRepository|.
class FakeLedgerRepositoryFactory : public fuchsia::ledger::internal::LedgerRepositoryFactory {
 public:
  struct GetRepositoryCall {
    zx::channel repository_handle;
    fidl::InterfaceHandle<fuchsia::ledger::cloud::CloudProvider> cloud_provider;
    std::string user_id;
    fidl::InterfaceRequest<fuchsia::ledger::internal::LedgerRepository> repository_request;

    GetRepositoryCall(
        zx::channel repository_handle,
        fidl::InterfaceHandle<fuchsia::ledger::cloud::CloudProvider> cloud_provider,
        std::string user_id,
        fidl::InterfaceRequest<fuchsia::ledger::internal::LedgerRepository> repository_request)
        : repository_handle(std::move(repository_handle)),
          cloud_provider(std::move(cloud_provider)),
          user_id(std::move(user_id)),
          repository_request(std::move(repository_request)) {}
  };

  explicit FakeLedgerRepositoryFactory(
      fidl::InterfaceRequest<fuchsia::ledger::internal::LedgerRepositoryFactory> request)
      : binding_(this, std::move(request)) {}

  ~FakeLedgerRepositoryFactory() override {}

  void Sync(fit::closure callback) override {
    FXL_NOTIMPLEMENTED();
    callback();
  }

  void GetRepository(zx::channel repository_handle,
                     fidl::InterfaceHandle<fuchsia::ledger::cloud::CloudProvider> cloud_provider,
                     std::string user_id,
                     fidl::InterfaceRequest<fuchsia::ledger::internal::LedgerRepository>
                         repository_request) override {
    get_repository_calls.emplace_back(std::move(repository_handle), std::move(cloud_provider),
                                      std::move(user_id), std::move(repository_request));
  }

  std::vector<GetRepositoryCall> get_repository_calls;

 private:
  fidl::Binding<fuchsia::ledger::internal::LedgerRepositoryFactory> binding_;
};

// Fake LedgerController that does nothing.
class FakeLedgerController : public fuchsia::ledger::internal::LedgerController {
 public:
  explicit FakeLedgerController(
      fidl::InterfaceRequest<fuchsia::ledger::internal::LedgerController> request)
      : binding_(this, std::move(request)) {}
  ~FakeLedgerController() override{};

  void Terminate() override { FXL_NOTIMPLEMENTED(); }

 private:
  fidl::Binding<fuchsia::ledger::internal::LedgerController> binding_;
};

// Verifies that Ledger is initialized with the right User ID from TokenManager.
TEST_F(SessionmgrImplTest, LedgerInitializedWithUserId) {
  sys::testing::ComponentContextProvider component_context_provider;
  fuchsia::modular::session::SessionmgrConfig config;
  config.set_component_args({});
  config.set_agent_service_index({});
  config.set_session_agents({});
  config.set_startup_agents({});
  config.set_enable_cobalt(false);
  config.set_enable_story_shell_preload(false);
  config.set_use_memfs_for_ledger(true);
  config.set_cloud_provider(fuchsia::modular::session::CloudProvider::LET_LEDGER_DECIDE);

  // URL of the shells. Actual value unimportant.
  std::string url = "test_url_string";

  // Sessionmgr asks for an environment. Let's provide one.
  std::vector<fidl::InterfaceRequest<fuchsia::sys::Environment>> environments;
  component_context_provider.service_directory_provider()->AddService<fuchsia::sys::Environment>(
      [&environments](fidl::InterfaceRequest<fuchsia::sys::Environment> request) {
        environments.push_back(std::move(request));
      });

  // Sessionmgr asks for a launcher, used to launch services (including Ledger). We need to
  // configure one and provide it.
  sys::testing::FakeLauncher fake_launcher;
  std::vector<std::unique_ptr<FakeComponentController>> component_controllers;
  fake_launcher.RegisterComponent(
      url, [&component_controllers](
               fuchsia::sys::LaunchInfo launch_info,
               fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller) {
        auto fake = std::make_unique<FakeComponentController>(std::move(launch_info),
                                                              std::move(controller));
        component_controllers.push_back(std::move(fake));
      });

  // Ledger is started as a component, and services are requested from it.
  sys::testing::FakeComponent ledger_component;
  std::vector<std::unique_ptr<FakeLedgerController>> ledger_controllers;
  std::vector<std::unique_ptr<FakeLedgerRepositoryFactory>> ledger_repository_factories;
  ledger_component.AddPublicService<fuchsia::ledger::internal::LedgerController>(
      [&ledger_controllers](
          fidl::InterfaceRequest<fuchsia::ledger::internal::LedgerController> handler) {
        ledger_controllers.push_back(std::make_unique<FakeLedgerController>(std::move(handler)));
      });
  ledger_component.AddPublicService<fuchsia::ledger::internal::LedgerRepositoryFactory>(
      [&ledger_repository_factories](
          fidl::InterfaceRequest<fuchsia::ledger::internal::LedgerRepositoryFactory> handler) {
        ledger_repository_factories.push_back(
            std::make_unique<FakeLedgerRepositoryFactory>(std::move(handler)));
      });
  ledger_component.Register(kLedgerAppUrl, fake_launcher);

  sys::testing::FakeComponent cloud_provider_component;
  std::vector<std::unique_ptr<FakeCloudProviderFactory>> cloud_provider_factories;
  cloud_provider_component.AddPublicService<fuchsia::ledger::cloud::firestore::Factory>(
      [&cloud_provider_factories](
          fidl::InterfaceRequest<fuchsia::ledger::cloud::firestore::Factory> handler) {
        cloud_provider_factories.push_back(
            std::make_unique<FakeCloudProviderFactory>(std::move(handler)));
      });
  cloud_provider_component.Register(kCloudProviderFirestoreAppUrl, fake_launcher);

  component_context_provider.service_directory_provider()->AddService(fake_launcher.GetHandler());

  fuchsia::modular::internal::SessionmgrPtr sessionmgr;
  SessionmgrImpl sessionmgr_impl(component_context_provider.context(), std::move(config));
  component_context_provider.ConnectToPublicService(sessionmgr.NewRequest());

  sessionmgr.set_error_handler([](zx_status_t status) {
    FXL_LOG(ERROR) << "Error when connected to Sessionmgr: " << status;
  });

  RunLoopUntilIdle();

  fuchsia::modular::AppConfig app_config;
  app_config.url = url;

  // We create the channels, even if we don't bind them on this side.
  fuchsia::auth::TokenManagerPtr agent_token_manager;
  auto agent_token_manager_request = agent_token_manager.NewRequest();
  fuchsia::modular::internal::SessionContextPtr session_context;
  auto session_context_request = session_context.NewRequest();
  fuchsia::auth::TokenManagerPtr ledger_token_manager;
  FakeTokenManager fake_token_manager(ledger_token_manager.NewRequest());

  auto account = std::make_unique<fuchsia::modular::auth::Account>();
  // Account ID, different from the user id.
  account->id = "1234567890";

  auto [view_token, view_token_holder] = scenic::ViewTokenPair::New();

  sessionmgr->Initialize("session_id", std::move(account), CloneStruct(app_config),
                         CloneStruct(app_config), false, std::move(ledger_token_manager),
                         std::move(agent_token_manager), std::move(session_context),
                         std::move(view_token));
  RunLoopUntilIdle();

  // Sessionmgr only finishes initialization once someone connects to it. Let's do that!
  ASSERT_THAT(component_controllers, SizeIs(1));
  fuchsia::modular::SessionShellContextPtr shell_context =
      component_controllers[0]->Connect<fuchsia::modular::SessionShellContext>();
  shell_context.set_error_handler(
      [](zx_status_t status) { FXL_LOG(ERROR) << "SessionShellContext disconnected: " << status; });

  RunLoopUntilIdle();

  // Verify that Ledger started with the right user profile ID.
  ASSERT_THAT(ledger_repository_factories, SizeIs(1));
  ASSERT_THAT(ledger_repository_factories[0]->get_repository_calls, SizeIs(1));
  EXPECT_THAT(ledger_repository_factories[0]->get_repository_calls[0].user_id, Eq(kProfileId));

  // Verify that the cloud provider started with the right user profile ID.
  ASSERT_THAT(cloud_provider_factories, SizeIs(1));
  ASSERT_THAT(cloud_provider_factories[0]->get_cloud_provider_requests, SizeIs(1));
  EXPECT_THAT(cloud_provider_factories[0]->get_cloud_provider_requests[0].config.user_profile_id,
              Eq(kProfileId));
}

}  // namespace
}  // namespace testing
}  // namespace modular
