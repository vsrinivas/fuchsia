// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/user_runner_impl.h"

#include <fcntl.h>
#include <memory>
#include <string>

#include <fuchsia/ledger/cloud/firestore/cpp/fidl.h>
#include <fuchsia/ledger/cpp/fidl.h>
#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <lib/component/cpp/connect.h>
#include <lib/fsl/io/fd.h>
#include <lib/fxl/files/directory.h>
#include <lib/fxl/files/unique_fd.h>
#include <lib/fxl/functional/make_copyable.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/device_runner/cobalt/cobalt.h"
#include "peridot/bin/user_runner/component_context_impl.h"
#include "peridot/bin/user_runner/device_map_impl.h"
#include "peridot/bin/user_runner/focus.h"
#include "peridot/bin/user_runner/message_queue/message_queue_manager.h"
#include "peridot/bin/user_runner/presentation_provider.h"
#include "peridot/bin/user_runner/puppet_master/make_production_impl.h"
#include "peridot/bin/user_runner/puppet_master/puppet_master_impl.h"
#include "peridot/bin/user_runner/puppet_master/story_command_executor.h"
#include "peridot/bin/user_runner/session_ctl.h"
#include "peridot/bin/user_runner/storage/constants_and_utils.h"
#include "peridot/bin/user_runner/storage/session_storage.h"
#include "peridot/bin/user_runner/story_runner/link_impl.h"
#include "peridot/bin/user_runner/story_runner/story_provider_impl.h"
#include "peridot/lib/common/names.h"
#include "peridot/lib/common/teardown.h"
#include "peridot/lib/common/xdr.h"
#include "peridot/lib/device_info/device_info.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "peridot/lib/fidl/scope.h"
#include "peridot/lib/ledger_client/constants.h"
#include "peridot/lib/ledger_client/ledger_client.h"
#include "peridot/lib/ledger_client/page_id.h"
#include "peridot/lib/ledger_client/status.h"

namespace modular {

// Maxwell doesn't yet implement lifecycle or has a lifecycle method, so we just
// let AppClient close the controller connection immediately. (The controller
// connection is closed once the ServiceTerminate() call invokes its done
// callback.)
template <>
void AppClient<fuchsia::modular::UserIntelligenceProviderFactory>::
    ServiceTerminate(const std::function<void()>& done) {
  done();
}

namespace {

constexpr char kAppId[] = "modular_user_runner";
constexpr char kMaxwellComponentNamespace[] = "maxwell";
constexpr char kMaxwellUrl[] = "maxwell";
constexpr char kContextEngineUrl[] = "context_engine";
constexpr char kContextEngineComponentNamespace[] = "context_engine";
constexpr char kModuleResolverUrl[] = "module_resolver";
constexpr char kUserScopeLabelPrefix[] = "user-";
constexpr char kMessageQueuePath[] = "/data/MESSAGE_QUEUES/v1/";
constexpr char kUserShellComponentNamespace[] = "user-shell-namespace";
constexpr char kUserShellLinkName[] = "user-shell-link";
constexpr char kLedgerDashboardUrl[] = "ledger_dashboard";
constexpr char kLedgerDashboardEnvLabel[] = "ledger-dashboard";
constexpr char kClipboardAgentUrl[] = "clipboard_agent";
constexpr char kLedgerRepositoryDirectory[] = "/data/LEDGER";

// The name in the outgoing debug directory (hub) for developer session control
// services.
constexpr char kSessionCtlDir[] = "sessionctl";

fuchsia::ledger::cloud::firestore::Config GetLedgerFirestoreConfig() {
  fuchsia::ledger::cloud::firestore::Config config;
  config.server_id = kFirebaseProjectId;
  config.api_key = kFirebaseApiKey;
  return config;
}

zx::channel GetLedgerRepositoryDirectory() {
  if (!files::CreateDirectory(kLedgerRepositoryDirectory)) {
    FXL_LOG(ERROR) << "Unable to create directory at "
                   << kLedgerRepositoryDirectory;
    return zx::channel();
  }
  fxl::UniqueFD dir(open(kLedgerRepositoryDirectory, O_PATH));
  if (!dir.is_valid()) {
    FXL_LOG(ERROR) << "Unable to open directory at "
                   << kLedgerRepositoryDirectory << ". errno: " << errno;
    return zx::channel();
  }

  return fsl::CloneChannelFromFileDescriptor(dir.get());
}

std::string GetAccountId(const fuchsia::modular::auth::AccountPtr& account) {
  return !account ? "GUEST" : account->id;
}

// Creates a function that can be used as termination action passed to AtEnd(),
// which when called invokes the reset() method on the object pointed to by the
// argument. Used to reset() fidl pointers and std::unique_ptr<>s fields.
template <typename X>
std::function<void(std::function<void()>)> Reset(
    std::unique_ptr<X>* const field) {
  return [field](std::function<void()> cont) {
    field->reset();
    cont();
  };
}

template <typename X>
std::function<void(std::function<void()>)> Reset(
    fidl::InterfacePtr<X>* const field) {
  return [field](std::function<void()> cont) {
    field->Unbind();
    cont();
  };
}

// Creates a function that can be used as termination action passed to AtEnd(),
// which when called asynchronously invokes the Teardown() method on the object
// pointed to by the argument. Used to teardown AppClient and AsyncHolder
// members.
template <typename X>
std::function<void(std::function<void()>)> Teardown(const zx::duration timeout,
                                                    const char* const message,
                                                    X* const field) {
  return [timeout, message, field](std::function<void()> cont) {
    field->Teardown(timeout, [message, cont] {
      if (message) {
        FXL_DLOG(INFO) << "- " << message << " down.";
      }
      cont();
    });
  };
}

}  // namespace

class UserRunnerImpl::PresentationProviderImpl : public PresentationProvider {
 public:
  PresentationProviderImpl(UserRunnerImpl* const impl) : impl_(impl) {}
  ~PresentationProviderImpl() override = default;

 private:
  // |PresentationProvider|
  void GetPresentation(fidl::StringPtr story_id,
                       fidl::InterfaceRequest<fuchsia::ui::policy::Presentation>
                           request) override {
    if (impl_->user_shell_app_) {
      fuchsia::modular::UserShellPresentationProviderPtr provider;
      impl_->user_shell_app_->services().ConnectToService(
          provider.NewRequest());
      provider->GetPresentation(std::move(story_id), std::move(request));
    }
  }

  void WatchVisualState(
      fidl::StringPtr story_id,
      fidl::InterfaceHandle<fuchsia::modular::StoryVisualStateWatcher> watcher)
      override {
    if (impl_->user_shell_app_) {
      fuchsia::modular::UserShellPresentationProviderPtr provider;
      impl_->user_shell_app_->services().ConnectToService(
          provider.NewRequest());
      provider->WatchVisualState(std::move(story_id), std::move(watcher));
    }
  }

  UserRunnerImpl* const impl_;
};

UserRunnerImpl::UserRunnerImpl(component::StartupContext* const startup_context,
                               const bool test)
    : startup_context_(startup_context),
      test_(test),
      user_shell_context_binding_(this),
      story_provider_impl_("StoryProviderImpl"),
      agent_runner_("AgentRunner") {
  startup_context_->outgoing()
      .AddPublicService<fuchsia::modular::internal::UserRunner>(
          [this](fidl::InterfaceRequest<fuchsia::modular::internal::UserRunner>
                     request) {
            bindings_.AddBinding(this, std::move(request));
          });
}

UserRunnerImpl::~UserRunnerImpl() = default;

void UserRunnerImpl::Initialize(
    fuchsia::modular::auth::AccountPtr account,
    fuchsia::modular::AppConfig user_shell,
    fuchsia::modular::AppConfig story_shell,
    fidl::InterfaceHandle<fuchsia::modular::auth::TokenProviderFactory>
        token_provider_factory,
    fidl::InterfaceHandle<fuchsia::modular::internal::UserContext> user_context,
    fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
        view_owner_request) {
  InitializeUser(std::move(account), std::move(token_provider_factory),
                 std::move(user_context));
  InitializeLedger();
  InitializeLedgerDashboard();
  InitializeDeviceMap();
  InitializeMessageQueueManager();
  InitializeMaxwellAndModular(user_shell.url, std::move(story_shell));
  InitializeClipboard();
  InitializeUserShell(std::move(user_shell), std::move(view_owner_request));

  ReportEvent(ModularEvent::BOOTED_TO_USER_RUNNER);
}

void UserRunnerImpl::InitializeUser(
    fuchsia::modular::auth::AccountPtr account,
    fidl::InterfaceHandle<fuchsia::modular::auth::TokenProviderFactory>
        token_provider_factory,
    fidl::InterfaceHandle<fuchsia::modular::internal::UserContext>
        user_context) {
  token_provider_factory_ = token_provider_factory.Bind();
  AtEnd(Reset(&token_provider_factory_));

  user_context_ = user_context.Bind();
  AtEnd(Reset(&user_context_));

  account_ = std::move(account);
  AtEnd(Reset(&account_));

  user_scope_ = std::make_unique<Scope>(
      startup_context_->environment(),
      std::string(kUserScopeLabelPrefix) + GetAccountId(account_));
  AtEnd(Reset(&user_scope_));
}

void UserRunnerImpl::InitializeLedger() {
  fuchsia::modular::AppConfig ledger_config;
  ledger_config.url = kLedgerAppUrl;

  fuchsia::sys::ServiceListPtr service_list = nullptr;
  if (account_) {
    service_list = fuchsia::sys::ServiceList::New();
    service_list->names.push_back(fuchsia::modular::auth::TokenProvider::Name_);
    ledger_service_provider_.AddService<fuchsia::modular::auth::TokenProvider>(
        [this](fidl::InterfaceRequest<fuchsia::modular::auth::TokenProvider>
                   request) {
          token_provider_factory_->GetTokenProvider(kLedgerAppUrl,
                                                    std::move(request));
        });
    ledger_service_provider_.AddBinding(service_list->provider.NewRequest());
  }

  ledger_app_ =
      std::make_unique<AppClient<fuchsia::ledger::internal::LedgerController>>(
          user_scope_->GetLauncher(), std::move(ledger_config), "",
          std::move(service_list));
  ledger_app_->SetAppErrorHandler([this] {
    FXL_LOG(ERROR) << "Ledger seems to have crashed unexpectedly." << std::endl
                   << "CALLING Logout() DUE TO UNRECOVERABLE LEDGER ERROR.";
    Logout();
  });
  AtEnd(Teardown(kBasicTimeout, "Ledger", ledger_app_.get()));

  fuchsia::ledger::cloud::CloudProviderPtr cloud_provider;
  if (account_) {
    // If not running in Guest mode, spin up a cloud provider for Ledger to use
    // for syncing.
    fuchsia::modular::AppConfig cloud_provider_config;
    cloud_provider_config.url = kCloudProviderFirestoreAppUrl;
    cloud_provider_config.args = fidl::VectorPtr<fidl::StringPtr>::New(0);
    cloud_provider_app_ =
        std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
            user_scope_->GetLauncher(), std::move(cloud_provider_config));
    cloud_provider_app_->services().ConnectToService(
        cloud_provider_factory_.NewRequest());

    cloud_provider = GetCloudProvider();

    // TODO(mesch): Teardown cloud_provider_app_ ?
  }

  ledger_app_->services().ConnectToService(
      ledger_repository_factory_.NewRequest());
  AtEnd(Reset(&ledger_repository_factory_));

  // The directory "/data" is the data root "/data/LEDGER" that the ledger app
  // client is configured to.
  ledger_repository_factory_->GetRepository(
      GetLedgerRepositoryDirectory(), std::move(cloud_provider),
      ledger_repository_.NewRequest(), [this](fuchsia::ledger::Status status) {
        if (status != fuchsia::ledger::Status::OK) {
          FXL_LOG(ERROR)
              << "LedgerRepositoryFactory.GetRepository() failed: "
              << LedgerStatusToString(status) << std::endl
              << "CALLING Logout() DUE TO UNRECOVERABLE LEDGER ERROR.";
          Logout();
        }
      });

  // If ledger state is erased from underneath us (happens when the cloud store
  // is cleared), ledger will close the connection to |ledger_repository_|.
  ledger_repository_.set_error_handler([this] { Logout(); });
  AtEnd(Reset(&ledger_repository_));

  ledger_client_.reset(
      new LedgerClient(ledger_repository_.get(), kAppId, [this] {
        FXL_LOG(ERROR) << "CALLING Logout() DUE TO UNRECOVERABLE LEDGER ERROR.";
        Logout();
      }));
  AtEnd(Reset(&ledger_client_));
}

void UserRunnerImpl::InitializeLedgerDashboard() {
  if (test_)
    return;
  ledger_dashboard_scope_ = std::make_unique<Scope>(
      user_scope_->environment(), std::string(kLedgerDashboardEnvLabel));
  AtEnd(Reset(&ledger_dashboard_scope_));

  ledger_dashboard_scope_->AddService<
      fuchsia::ledger::internal::LedgerRepositoryDebug>(
      [this](fidl::InterfaceRequest<
             fuchsia::ledger::internal::LedgerRepositoryDebug>
                 request) {
        if (ledger_repository_) {
          ledger_repository_->GetLedgerRepositoryDebug(
              std::move(request), [](fuchsia::ledger::Status status) {
                if (status != fuchsia::ledger::Status::OK) {
                  FXL_LOG(ERROR)
                      << "LedgerRepository.GetLedgerRepositoryDebug() failed: "
                      << LedgerStatusToString(status);
                }
              });
        }
      });

  fuchsia::modular::AppConfig ledger_dashboard_config;
  ledger_dashboard_config.url = kLedgerDashboardUrl;

  ledger_dashboard_app_ =
      std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
          ledger_dashboard_scope_->GetLauncher(),
          std::move(ledger_dashboard_config));

  AtEnd(Reset(&ledger_dashboard_app_));
  AtEnd(
      Teardown(kBasicTimeout, "LedgerDashboard", ledger_dashboard_app_.get()));

  FXL_LOG(INFO) << "Starting Ledger dashboard " << kLedgerDashboardUrl;
}

void UserRunnerImpl::InitializeDeviceMap() {
  // fuchsia::modular::DeviceMap service
  const std::string device_id = LoadDeviceID(GetAccountId(account_));
  device_name_ = LoadDeviceName(GetAccountId(account_));
  const std::string device_profile = LoadDeviceProfile();

  device_map_impl_ = std::make_unique<DeviceMapImpl>(
      device_name_, device_id, device_profile, ledger_client_.get(),
      fuchsia::ledger::PageId());
  user_scope_->AddService<fuchsia::modular::DeviceMap>(
      [this](fidl::InterfaceRequest<fuchsia::modular::DeviceMap> request) {
        // device_map_impl_ may be reset before user_scope_.
        if (device_map_impl_) {
          device_map_impl_->Connect(std::move(request));
        }
      });
  AtEnd(Reset(&device_map_impl_));
}

void UserRunnerImpl::InitializeClipboard() {
  agent_runner_->ConnectToAgent(kAppId, kClipboardAgentUrl,
                                services_from_clipboard_agent_.NewRequest(),
                                clipboard_agent_controller_.NewRequest());
  user_scope_->AddService<fuchsia::modular::Clipboard>(
      [this](fidl::InterfaceRequest<fuchsia::modular::Clipboard> request) {
        services_from_clipboard_agent_->ConnectToService(
            fuchsia::modular::Clipboard::Name_, request.TakeChannel());
      });
}

void UserRunnerImpl::InitializeMessageQueueManager() {
  std::string message_queue_path = kMessageQueuePath;
  message_queue_path.append(GetAccountId(account_));
  if (!files::CreateDirectory(message_queue_path)) {
    FXL_LOG(FATAL) << "Failed to create message queue directory: "
                   << message_queue_path;
  }

  message_queue_manager_ = std::make_unique<MessageQueueManager>(
      ledger_client_.get(), MakePageId(kMessageQueuePageId),
      message_queue_path);
  AtEnd(Reset(&message_queue_manager_));
}

void UserRunnerImpl::InitializeMaxwellAndModular(
    const fidl::StringPtr& user_shell_url,
    fuchsia::modular::AppConfig story_shell) {
  // NOTE: There is an awkward service exchange here between
  // fuchsia::modular::UserIntelligenceProvider, AgentRunner, StoryProviderImpl,
  // FocusHandler, VisibleStoriesHandler.
  //
  // AgentRunner needs a fuchsia::modular::UserIntelligenceProvider to expose
  // services from Maxwell through its GetIntelligenceServices() method.
  // Initializing the Maxwell process (through
  // fuchsia::modular::UserIntelligenceProviderFactory) requires a
  // fuchsia::modular::ComponentContext. fuchsia::modular::ComponentContext
  // requires an AgentRunner, which creates a circular dependency.
  //
  // Because of FIDL late bindings, we can get around this by creating
  // a new InterfaceRequest here (|intelligence_provider_request|),
  // making the InterfacePtr a valid proxy to be passed to AgentRunner
  // and StoryProviderImpl, even though it won't be bound to a real
  // implementation (provided by Maxwell) until later. It works, but
  // it's not a good pattern.
  //
  // A similar relationship holds between FocusHandler and
  // fuchsia::modular::UserIntelligenceProvider.
  auto intelligence_provider_request = user_intelligence_provider_.NewRequest();
  AtEnd(Reset(&user_intelligence_provider_));

  fidl::InterfaceHandle<fuchsia::modular::ContextEngine> context_engine;
  auto context_engine_request = context_engine.NewRequest();

  fidl::InterfaceHandle<fuchsia::modular::StoryProvider> story_provider;
  auto story_provider_request = story_provider.NewRequest();

  fidl::InterfaceHandle<fuchsia::modular::FocusProvider> focus_provider_maxwell;
  auto focus_provider_request_maxwell = focus_provider_maxwell.NewRequest();

  fidl::InterfaceHandle<fuchsia::modular::PuppetMaster> puppet_master;
  auto puppet_master_request = puppet_master.NewRequest();

  fidl::InterfaceHandle<fuchsia::modular::VisibleStoriesProvider>
      visible_stories_provider;
  auto visible_stories_provider_request = visible_stories_provider.NewRequest();

  // Start kMaxwellUrl
  fuchsia::modular::AppConfig maxwell_config;
  maxwell_config.url = kMaxwellUrl;
  if (test_) {
    // TODO(mesch): This path name is local to the maxwell package. It should
    // not be exposed outside it at all. Presumably just pass --test.
    maxwell_config.args.push_back(
        "--config=/pkg/data/maxwell/test_config.json");
  }

  maxwell_app_ = std::make_unique<
      AppClient<fuchsia::modular::UserIntelligenceProviderFactory>>(
      user_scope_->GetLauncher(), std::move(maxwell_config));
  maxwell_app_->primary_service()->GetUserIntelligenceProvider(
      std::move(context_engine), std::move(story_provider),
      std::move(focus_provider_maxwell), std::move(visible_stories_provider),
      std::move(puppet_master), std::move(intelligence_provider_request));
  AtEnd(Reset(&maxwell_app_));
  AtEnd(Teardown(kBasicTimeout, "Maxwell", maxwell_app_.get()));

  entity_provider_runner_ = std::make_unique<EntityProviderRunner>(
      static_cast<EntityProviderLauncher*>(this));
  AtEnd(Reset(&entity_provider_runner_));

  agent_runner_storage_ = std::make_unique<AgentRunnerStorageImpl>(
      ledger_client_.get(), MakePageId(kAgentRunnerPageId));
  AtEnd(Reset(&agent_runner_storage_));

  agent_runner_.reset(new AgentRunner(
      user_scope_->GetLauncher(), message_queue_manager_.get(),
      ledger_repository_.get(), agent_runner_storage_.get(),
      token_provider_factory_.get(), user_intelligence_provider_.get(),
      entity_provider_runner_.get()));
  AtEnd(Teardown(kAgentRunnerTimeout, "AgentRunner", &agent_runner_));

  maxwell_component_context_bindings_ = std::make_unique<
      fidl::BindingSet<fuchsia::modular::ComponentContext,
                       std::unique_ptr<ComponentContextImpl>>>();
  AtEnd(Reset(&maxwell_component_context_bindings_));

  ComponentContextInfo component_context_info{
      message_queue_manager_.get(), agent_runner_.get(),
      ledger_repository_.get(), entity_provider_runner_.get()};
  // Start kContextEngineUrl.
  {
    context_engine_ns_services_.AddService<fuchsia::modular::ComponentContext>(
        [this, component_context_info](
            fidl::InterfaceRequest<fuchsia::modular::ComponentContext>
                request) {
          maxwell_component_context_bindings_->AddBinding(
              std::make_unique<ComponentContextImpl>(
                  component_context_info, kContextEngineComponentNamespace,
                  kContextEngineUrl, kContextEngineUrl),
              std::move(request));
        });
    auto service_list = fuchsia::sys::ServiceList::New();
    service_list->names.push_back(fuchsia::modular::ComponentContext::Name_);
    context_engine_ns_services_.AddBinding(service_list->provider.NewRequest());

    fuchsia::modular::AppConfig context_engine_config;
    context_engine_config.url = kContextEngineUrl;

    context_engine_app_ =
        std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
            user_scope_->GetLauncher(), std::move(context_engine_config),
            "" /* data_origin */, std::move(service_list));
    context_engine_app_->services().ConnectToService(
        std::move(context_engine_request));
    AtEnd(Reset(&context_engine_app_));
    AtEnd(Teardown(kBasicTimeout, "ContextEngine", context_engine_app_.get()));
  }

  auto maxwell_app_component_context =
      maxwell_component_context_bindings_->AddBinding(
          std::make_unique<ComponentContextImpl>(component_context_info,
                                                 kMaxwellComponentNamespace,
                                                 kMaxwellUrl, kMaxwellUrl));
  user_intelligence_provider_->StartAgents(
      std::move(maxwell_app_component_context));

  // Setup for kModuleResolverUrl
  {
    module_resolver_ns_services_
        .AddService<fuchsia::modular::IntelligenceServices>(
            [this](
                fidl::InterfaceRequest<fuchsia::modular::IntelligenceServices>
                    request) {
              fuchsia::modular::ComponentScope component_scope;
              component_scope.set_global_scope(fuchsia::modular::GlobalScope());
              fidl::InterfaceHandle<fuchsia::modular::IntelligenceServices>
                  intelligence_services;
              if (user_intelligence_provider_) {
                user_intelligence_provider_->GetComponentIntelligenceServices(
                    std::move(component_scope), std::move(request));
              }
            });
    module_resolver_ns_services_.AddService<fuchsia::modular::ComponentContext>(
        [this, component_context_info](
            fidl::InterfaceRequest<fuchsia::modular::ComponentContext>
                request) {
          maxwell_component_context_bindings_->AddBinding(
              std::make_unique<ComponentContextImpl>(
                  component_context_info, kMaxwellComponentNamespace,
                  kModuleResolverUrl, kModuleResolverUrl),
              std::move(request));
        });
    auto service_list = fuchsia::sys::ServiceList::New();
    service_list->names.push_back(
        fuchsia::modular::IntelligenceServices::Name_);
    service_list->names.push_back(fuchsia::modular::ComponentContext::Name_);
    module_resolver_ns_services_.AddBinding(
        service_list->provider.NewRequest());

    fuchsia::modular::AppConfig module_resolver_config;
    module_resolver_config.url = kModuleResolverUrl;
    if (test_) {
      module_resolver_config.args.push_back("--test");
    }
    // For now, we want data_origin to be "", which uses our (parent process's)
    // /data. This is appropriate for the module_resolver. We can in the future
    // isolate the data it reads to a subdir of /data and map that in here.
    module_resolver_app_ =
        std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
            user_scope_->GetLauncher(), std::move(module_resolver_config),
            "" /* data_origin */, std::move(service_list));
    AtEnd(Reset(&module_resolver_app_));
    AtEnd(Teardown(kBasicTimeout, "Resolver", module_resolver_app_.get()));
  }

  module_resolver_app_->services().ConnectToService(
      module_resolver_service_.NewRequest());
  AtEnd(Reset(&module_resolver_service_));
  // End kModuleResolverUrl

  user_shell_component_context_impl_ = std::make_unique<ComponentContextImpl>(
      component_context_info, kUserShellComponentNamespace, user_shell_url,
      user_shell_url);

  AtEnd(Reset(&user_shell_component_context_impl_));

  fidl::InterfacePtr<fuchsia::modular::FocusProvider>
      focus_provider_story_provider;
  auto focus_provider_request_story_provider =
      focus_provider_story_provider.NewRequest();

  presentation_provider_impl_.reset(new PresentationProviderImpl(this));
  AtEnd(Reset(&presentation_provider_impl_));

  // We create |story_provider_impl_| after |agent_runner_| so
  // story_provider_impl_ is termiated before agent_runner_, which will cause
  // all modules to be terminated before agents are terminated. Agents must
  // outlive the stories which contain modules that are connected to those
  // agents.
  session_storage_.reset(
      new SessionStorage(ledger_client_.get(), fuchsia::ledger::PageId()));
  AtEnd(Reset(&session_storage_));
  story_provider_impl_.reset(new StoryProviderImpl(
      user_scope_.get(), device_map_impl_->current_device_id(),
      session_storage_.get(), std::move(story_shell), component_context_info,
      std::move(focus_provider_story_provider),
      user_intelligence_provider_.get(), module_resolver_service_.get(),
      entity_provider_runner_.get(), presentation_provider_impl_.get(), test_));
  story_provider_impl_->Connect(std::move(story_provider_request));

  AtEnd(
      Teardown(kStoryProviderTimeout, "StoryProvider", &story_provider_impl_));

  fuchsia::modular::FocusProviderPtr focus_provider_puppet_master;
  auto focus_provider_request_puppet_master =
      focus_provider_puppet_master.NewRequest();
  // Initialize the PuppetMaster.
  story_command_executor_ = MakeProductionStoryCommandExecutor(
      session_storage_.get(), std::move(focus_provider_puppet_master),
      module_resolver_service_.get(), entity_provider_runner_.get());
  puppet_master_impl_.reset(new PuppetMasterImpl(
      session_storage_.get(), story_command_executor_.get()));
  puppet_master_impl_->Connect(std::move(puppet_master_request));

  session_ctl_.reset(new SessionCtl(startup_context_->outgoing().debug_dir(),
                                    kSessionCtlDir, puppet_master_impl_.get()));

  AtEnd(Reset(&story_command_executor_));
  AtEnd(Reset(&puppet_master_impl_));
  AtEnd(Reset(&session_ctl_));

  focus_handler_ = std::make_unique<FocusHandler>(
      device_map_impl_->current_device_id(), ledger_client_.get(),
      fuchsia::ledger::PageId());
  focus_handler_->AddProviderBinding(std::move(focus_provider_request_maxwell));
  focus_handler_->AddProviderBinding(
      std::move(focus_provider_request_story_provider));
  focus_handler_->AddProviderBinding(
      std::move(focus_provider_request_puppet_master));

  visible_stories_handler_ = std::make_unique<VisibleStoriesHandler>();
  visible_stories_handler_->AddProviderBinding(
      std::move(visible_stories_provider_request));

  AtEnd(Reset(&focus_handler_));
  AtEnd(Reset(&visible_stories_handler_));
}

void UserRunnerImpl::InitializeUserShell(
    fuchsia::modular::AppConfig user_shell,
    fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
        view_owner_request) {
  // We setup our own view and make the fuchsia::modular::UserShell a child of
  // it.
  user_shell_view_host_ = std::make_unique<ViewHost>(
      startup_context_
          ->ConnectToEnvironmentService<fuchsia::ui::viewsv1::ViewManager>(),
      std::move(view_owner_request));
  RunUserShell(std::move(user_shell));
  AtEnd([this](std::function<void()> cont) { TerminateUserShell(cont); });
}

void UserRunnerImpl::RunUserShell(fuchsia::modular::AppConfig user_shell) {
  user_shell_app_ = std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
      user_scope_->GetLauncher(), std::move(user_shell));

  if (user_shell_.is_bound()) {
    user_shell_.Unbind();
  }
  user_shell_app_->services().ConnectToService(user_shell_.NewRequest());

  user_shell_app_->SetAppErrorHandler([this] {
    FXL_LOG(ERROR) << "User Shell seems to have crashed unexpectedly."
                   << "Logging out.";
    Logout();
  });

  fuchsia::ui::viewsv1token::ViewOwnerPtr view_owner;
  fuchsia::ui::viewsv1::ViewProviderPtr view_provider;
  user_shell_app_->services().ConnectToService(view_provider.NewRequest());
  view_provider->CreateView(view_owner.NewRequest(), nullptr);
  user_shell_view_host_->ConnectView(std::move(view_owner));

  if (user_shell_context_binding_.is_bound()) {
    user_shell_context_binding_.Unbind();
  }
  user_shell_->Initialize(user_shell_context_binding_.NewBinding());
}

void UserRunnerImpl::TerminateUserShell(const std::function<void()>& done) {
  user_shell_app_->Teardown(kBasicTimeout, [this, done] {
    user_shell_.Unbind();
    user_shell_app_.reset();
    done();
  });
}

class UserRunnerImpl::SwapUserShellOperation : public Operation<> {
 public:
  SwapUserShellOperation(UserRunnerImpl* const user_runner_impl,
                         fuchsia::modular::AppConfig user_shell_config,
                         ResultCall result_call)
      : Operation("UserRunnerImpl::SwapUserShellOperation",
                  std::move(result_call)),
        user_runner_impl_(user_runner_impl),
        user_shell_config_(std::move(user_shell_config)) {}

 private:
  void Run() override {
    FlowToken flow{this};
    user_runner_impl_->story_provider_impl_->StopAllStories([this, flow] {
      user_runner_impl_->TerminateUserShell([this, flow] {
        user_runner_impl_->RunUserShell(std::move(user_shell_config_));
      });
    });
  }

  UserRunnerImpl* const user_runner_impl_;
  fuchsia::modular::AppConfig user_shell_config_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SwapUserShellOperation);
};

void UserRunnerImpl::SwapUserShell(
    fuchsia::modular::AppConfig user_shell_config,
    SwapUserShellCallback callback) {
  operation_queue_.Add(
      new SwapUserShellOperation(this, std::move(user_shell_config), callback));
}

void UserRunnerImpl::Terminate(std::function<void()> done) {
  FXL_LOG(INFO) << "UserRunner::Terminate()";
  at_end_done_ = std::move(done);

  TerminateRecurse(at_end_.size() - 1);
}

void UserRunnerImpl::GetAccount(GetAccountCallback callback) {
  callback(fidl::Clone(account_));
}

void UserRunnerImpl::GetAgentProvider(
    fidl::InterfaceRequest<fuchsia::modular::AgentProvider> request) {
  agent_runner_->Connect(std::move(request));
}

void UserRunnerImpl::GetComponentContext(
    fidl::InterfaceRequest<fuchsia::modular::ComponentContext> request) {
  user_shell_component_context_impl_->Connect(std::move(request));
}

void UserRunnerImpl::GetDeviceName(GetDeviceNameCallback callback) {
  callback(device_name_);
}

void UserRunnerImpl::GetFocusController(
    fidl::InterfaceRequest<fuchsia::modular::FocusController> request) {
  focus_handler_->AddControllerBinding(std::move(request));
}

void UserRunnerImpl::GetFocusProvider(
    fidl::InterfaceRequest<fuchsia::modular::FocusProvider> request) {
  focus_handler_->AddProviderBinding(std::move(request));
}

void UserRunnerImpl::GetIntelligenceServices(
    fidl::InterfaceRequest<fuchsia::modular::IntelligenceServices> request) {
  fuchsia::modular::ComponentScope component_scope;
  component_scope.set_global_scope(fuchsia::modular::GlobalScope());
  user_intelligence_provider_->GetComponentIntelligenceServices(
      std::move(component_scope), std::move(request));
}

void UserRunnerImpl::GetLink(
    fidl::InterfaceRequest<fuchsia::modular::Link> request) {
  if (!user_shell_storage_) {
    user_shell_storage_ = std::make_unique<StoryStorage>(
        ledger_client_.get(), fuchsia::ledger::PageId());
  }

  fuchsia::modular::LinkPath link_path;
  link_path.module_path.resize(0);
  link_path.link_name = kUserShellLinkName;
  auto impl = std::make_unique<LinkImpl>(user_shell_storage_.get(),
                                         std::move(link_path));
  user_shell_link_bindings_.AddBinding(std::move(impl), std::move(request));
}

void UserRunnerImpl::GetPresentation(
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) {
  user_context_->GetPresentation(std::move(request));
}

void UserRunnerImpl::GetSpeechToText(
    fidl::InterfaceRequest<fuchsia::speech::SpeechToText> request) {
  user_intelligence_provider_->GetSpeechToText(std::move(request));
}

void UserRunnerImpl::GetStoryProvider(
    fidl::InterfaceRequest<fuchsia::modular::StoryProvider> request) {
  story_provider_impl_->Connect(std::move(request));
}

void UserRunnerImpl::GetSuggestionProvider(
    fidl::InterfaceRequest<fuchsia::modular::SuggestionProvider> request) {
  user_intelligence_provider_->GetSuggestionProvider(std::move(request));
}

void UserRunnerImpl::GetVisibleStoriesController(
    fidl::InterfaceRequest<fuchsia::modular::VisibleStoriesController>
        request) {
  visible_stories_handler_->AddControllerBinding(std::move(request));
}

void UserRunnerImpl::Logout() { user_context_->Logout(); }

// |EntityProviderLauncher|
void UserRunnerImpl::ConnectToEntityProvider(
    const std::string& agent_url,
    fidl::InterfaceRequest<fuchsia::modular::EntityProvider>
        entity_provider_request,
    fidl::InterfaceRequest<fuchsia::modular::AgentController>
        agent_controller_request) {
  FXL_DCHECK(agent_runner_.get());
  agent_runner_->ConnectToEntityProvider(agent_url,
                                         std::move(entity_provider_request),
                                         std::move(agent_controller_request));
}

fuchsia::ledger::cloud::CloudProviderPtr UserRunnerImpl::GetCloudProvider() {
  fuchsia::ledger::cloud::CloudProviderPtr cloud_provider;
  fidl::InterfaceHandle<fuchsia::modular::auth::TokenProvider>
      ledger_token_provider;
  token_provider_factory_->GetTokenProvider(kLedgerAppUrl,
                                            ledger_token_provider.NewRequest());
  auto cloud_provider_config = GetLedgerFirestoreConfig();

  cloud_provider_factory_->GetCloudProvider(
      std::move(cloud_provider_config), std::move(ledger_token_provider),
      cloud_provider.NewRequest(), [](fuchsia::ledger::cloud::Status status) {
        if (status != fuchsia::ledger::cloud::Status::OK) {
          FXL_LOG(ERROR) << "Failed to create a cloud provider: "
                         << fidl::ToUnderlying(status);
        }
      });
  return cloud_provider;
}

void UserRunnerImpl::AtEnd(std::function<void(std::function<void()>)> action) {
  at_end_.emplace_back(std::move(action));
}

void UserRunnerImpl::TerminateRecurse(const int i) {
  if (i >= 0) {
    at_end_[i]([this, i] { TerminateRecurse(i - 1); });
  } else {
    FXL_LOG(INFO) << "UserRunner::Terminate(): done";
    at_end_done_();
  }
}

}  // namespace modular
