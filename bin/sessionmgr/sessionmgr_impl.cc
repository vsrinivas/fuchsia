// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/sessionmgr_impl.h"

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
#include <lib/fsl/types/type_converters.h>
#include <lib/fxl/files/directory.h>
#include <lib/fxl/files/unique_fd.h>
#include <lib/fxl/functional/make_copyable.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/basemgr/cobalt/cobalt.h"
#include "peridot/bin/sessionmgr/component_context_impl.h"
#include "peridot/bin/sessionmgr/device_map_impl.h"
#include "peridot/bin/sessionmgr/focus.h"
#include "peridot/bin/sessionmgr/message_queue/message_queue_manager.h"
#include "peridot/bin/sessionmgr/presentation_provider.h"
#include "peridot/bin/sessionmgr/puppet_master/make_production_impl.h"
#include "peridot/bin/sessionmgr/puppet_master/puppet_master_impl.h"
#include "peridot/bin/sessionmgr/puppet_master/story_command_executor.h"
#include "peridot/bin/sessionmgr/session_ctl.h"
#include "peridot/bin/sessionmgr/storage/constants_and_utils.h"
#include "peridot/bin/sessionmgr/storage/session_storage.h"
#include "peridot/bin/sessionmgr/story_runner/link_impl.h"
#include "peridot/bin/sessionmgr/story_runner/story_provider_impl.h"
#include "peridot/lib/common/names.h"
#include "peridot/lib/common/teardown.h"
#include "peridot/lib/common/xdr.h"
#include "peridot/lib/device_info/device_info.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/environment.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "peridot/lib/ledger_client/constants.h"
#include "peridot/lib/ledger_client/ledger_client.h"
#include "peridot/lib/ledger_client/page_id.h"
#include "peridot/lib/ledger_client/status.h"

namespace modular {

namespace {

constexpr char kAppId[] = "modular_sessionmgr";
constexpr char kMaxwellComponentNamespace[] = "maxwell";
constexpr char kMaxwellUrl[] = "maxwell";
constexpr char kContextEngineUrl[] = "context_engine";
constexpr char kContextEngineComponentNamespace[] = "context_engine";
constexpr char kModuleResolverUrl[] = "module_resolver";
constexpr char kUserEnvironmentLabelPrefix[] = "user-";
constexpr char kMessageQueuePath[] = "/data/MESSAGE_QUEUES/v1/";
constexpr char kSessionShellComponentNamespace[] = "user-shell-namespace";
constexpr char kSessionShellLinkName[] = "user-shell-link";
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

class SessionmgrImpl::PresentationProviderImpl : public PresentationProvider {
 public:
  PresentationProviderImpl(SessionmgrImpl* const impl) : impl_(impl) {}
  ~PresentationProviderImpl() override = default;

 private:
  // |PresentationProvider|
  void GetPresentation(fidl::StringPtr story_id,
                       fidl::InterfaceRequest<fuchsia::ui::policy::Presentation>
                           request) override {
    if (impl_->session_shell_app_) {
      fuchsia::modular::SessionShellPresentationProviderPtr provider;
      impl_->session_shell_app_->services().ConnectToService(
          provider.NewRequest());
      provider->GetPresentation(std::move(story_id), std::move(request));
    }
  }

  void WatchVisualState(
      fidl::StringPtr story_id,
      fidl::InterfaceHandle<fuchsia::modular::StoryVisualStateWatcher> watcher)
      override {
    if (impl_->session_shell_app_) {
      fuchsia::modular::SessionShellPresentationProviderPtr provider;
      impl_->session_shell_app_->services().ConnectToService(
          provider.NewRequest());
      provider->WatchVisualState(std::move(story_id), std::move(watcher));
    }
  }

  SessionmgrImpl* const impl_;
};

SessionmgrImpl::SessionmgrImpl(component::StartupContext* const startup_context,
                               const Options& options)
    : startup_context_(startup_context),
      options_(options),
      story_provider_impl_("StoryProviderImpl"),
      agent_runner_("AgentRunner") {
  startup_context_->outgoing()
      .AddPublicService<fuchsia::modular::internal::Sessionmgr>(
          [this](fidl::InterfaceRequest<fuchsia::modular::internal::Sessionmgr>
                     request) {
            bindings_.AddBinding(this, std::move(request));
          });
}

SessionmgrImpl::~SessionmgrImpl() = default;

void SessionmgrImpl::Initialize(
    fuchsia::modular::auth::AccountPtr account,
    fuchsia::modular::AppConfig session_shell,
    fuchsia::modular::AppConfig story_shell,
    fidl::InterfaceHandle<fuchsia::modular::auth::TokenProviderFactory>
        token_provider_factory,
    fidl::InterfaceHandle<fuchsia::auth::TokenManager> ledger_token_manager,
    fidl::InterfaceHandle<fuchsia::auth::TokenManager> agent_token_manager,
    fidl::InterfaceHandle<fuchsia::modular::internal::UserContext> user_context,
    fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
        view_owner_request) {
  InitializeUser(std::move(account), std::move(token_provider_factory),
                 std::move(agent_token_manager), std::move(user_context));
  InitializeLedger(std::move(ledger_token_manager));
  InitializeLedgerDashboard();
  InitializeDeviceMap();
  InitializeMessageQueueManager();
  InitializeMaxwellAndModular(session_shell.url, std::move(story_shell));
  InitializeClipboard();
  InitializeSessionShell(std::move(session_shell),
                         std::move(view_owner_request));

  ReportEvent(ModularEvent::BOOTED_TO_SESSIONMGR);
}

void SessionmgrImpl::InitializeUser(
    fuchsia::modular::auth::AccountPtr account,
    fidl::InterfaceHandle<fuchsia::modular::auth::TokenProviderFactory>
        token_provider_factory,
    fidl::InterfaceHandle<fuchsia::auth::TokenManager> agent_token_manager,
    fidl::InterfaceHandle<fuchsia::modular::internal::UserContext>
        user_context) {
  if (token_provider_factory.is_valid()) {
    token_provider_factory_ = token_provider_factory.Bind();
    AtEnd(Reset(&token_provider_factory_));
  } else {
    agent_token_manager_ = agent_token_manager.Bind();
    AtEnd(Reset(&agent_token_manager_));
  }

  user_context_ = user_context.Bind();
  AtEnd(Reset(&user_context_));

  account_ = std::move(account);
  AtEnd(Reset(&account_));

  static const auto* const kEnvServices = new std::vector<std::string>{
      fuchsia::modular::DeviceMap::Name_, fuchsia::modular::Clipboard::Name_};
  user_environment_ = std::make_unique<Environment>(
      startup_context_->environment(),
      std::string(kUserEnvironmentLabelPrefix) + GetAccountId(account_),
      *kEnvServices,
      /* kill_on_oom = */ true);
  AtEnd(Reset(&user_environment_));
}

zx::channel SessionmgrImpl::GetLedgerRepositoryDirectory() {
  if (options_.use_memfs_for_ledger) {
    FXL_DCHECK(!memfs_for_ledger_)
        << "An existing memfs for the Ledger has already been initialized.";
    FXL_LOG(INFO) << "Using memfs-backed storage for the ledger.";
    memfs_for_ledger_ = std::make_unique<scoped_tmpfs::ScopedTmpFS>();
    AtEnd(Reset(&memfs_for_ledger_));

    return fsl::CloneChannelFromFileDescriptor(memfs_for_ledger_->root_fd());
  }
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

void SessionmgrImpl::InitializeLedger(
    fidl::InterfaceHandle<fuchsia::auth::TokenManager> ledger_token_manager) {
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
          user_environment_->GetLauncher(), std::move(ledger_config), "",
          std::move(service_list));
  ledger_app_->SetAppErrorHandler([this] {
    FXL_LOG(ERROR) << "Ledger seems to have crashed unexpectedly." << std::endl
                   << "CALLING Logout() DUE TO UNRECOVERABLE LEDGER ERROR.";
    Logout();
  });
  AtEnd(Teardown(kBasicTimeout, "Ledger", ledger_app_.get()));

  fuchsia::ledger::cloud::CloudProviderPtr cloud_provider;
  if (account_ && !options_.no_cloud_provider_for_ledger) {
    // If not running in Guest mode, spin up a cloud provider for Ledger to use
    // for syncing.
    fuchsia::modular::AppConfig cloud_provider_config;
    cloud_provider_config.url = kCloudProviderFirestoreAppUrl;
    cloud_provider_config.args = fidl::VectorPtr<fidl::StringPtr>::New(0);
    cloud_provider_app_ =
        std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
            user_environment_->GetLauncher(), std::move(cloud_provider_config));
    cloud_provider_app_->services().ConnectToService(
        cloud_provider_factory_.NewRequest());

    cloud_provider = GetCloudProvider();

    // TODO(mesch): Teardown cloud_provider_app_ ?
  }

  ledger_repository_factory_.set_error_handler([this](zx_status_t status) {
    FXL_LOG(ERROR) << "LedgerRepositoryFactory.GetRepository() failed: "
                   << LedgerEpitaphToString(status) << std::endl
                   << "CALLING Logout() DUE TO UNRECOVERABLE LEDGER ERROR.";
    Logout();
  });
  ledger_app_->services().ConnectToService(
      ledger_repository_factory_.NewRequest());
  AtEnd(Reset(&ledger_repository_factory_));

  // The directory "/data" is the data root "/data/LEDGER" that the ledger app
  // client is configured to.
  ledger_repository_factory_->GetRepository(GetLedgerRepositoryDirectory(),
                                            std::move(cloud_provider),
                                            ledger_repository_.NewRequest());

  // If ledger state is erased from underneath us (happens when the cloud store
  // is cleared), ledger will close the connection to |ledger_repository_|.
  ledger_repository_.set_error_handler(
      [this](zx_status_t status) { Logout(); });
  AtEnd(Reset(&ledger_repository_));

  ledger_client_ =
      std::make_unique<LedgerClient>(ledger_repository_.get(), kAppId, [this] {
        FXL_LOG(ERROR) << "CALLING Logout() DUE TO UNRECOVERABLE LEDGER ERROR.";
        Logout();
      });
  AtEnd(Reset(&ledger_client_));
}

void SessionmgrImpl::InitializeLedgerDashboard() {
  if (options_.test)
    return;
  static const auto* const kEnvServices = new std::vector<std::string>{
      fuchsia::ledger::internal::LedgerRepositoryDebug::Name_};
  ledger_dashboard_environment_ = std::make_unique<Environment>(
      user_environment_->environment(), std::string(kLedgerDashboardEnvLabel),
      *kEnvServices, /* kill_on_oom = */ false);
  AtEnd(Reset(&ledger_dashboard_environment_));

  ledger_dashboard_environment_->AddService<
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
          ledger_dashboard_environment_->GetLauncher(),
          std::move(ledger_dashboard_config));

  AtEnd(Reset(&ledger_dashboard_app_));
  AtEnd(
      Teardown(kBasicTimeout, "LedgerDashboard", ledger_dashboard_app_.get()));

  FXL_LOG(INFO) << "Starting Ledger dashboard " << kLedgerDashboardUrl;
}

void SessionmgrImpl::InitializeDeviceMap() {
  // fuchsia::modular::DeviceMap service
  const std::string device_id = LoadDeviceID(GetAccountId(account_));
  device_name_ = LoadDeviceName(GetAccountId(account_));
  const std::string device_profile = LoadDeviceProfile();

  device_map_impl_ = std::make_unique<DeviceMapImpl>(
      device_name_, device_id, device_profile, ledger_client_.get(),
      fuchsia::ledger::PageId());
  user_environment_->AddService<fuchsia::modular::DeviceMap>(
      [this](fidl::InterfaceRequest<fuchsia::modular::DeviceMap> request) {
        // device_map_impl_ may be reset before user_environment_.
        if (device_map_impl_) {
          device_map_impl_->Connect(std::move(request));
        }
      });
  AtEnd(Reset(&device_map_impl_));
}

void SessionmgrImpl::InitializeClipboard() {
  agent_runner_->ConnectToAgent(kAppId, kClipboardAgentUrl,
                                services_from_clipboard_agent_.NewRequest(),
                                clipboard_agent_controller_.NewRequest());
  user_environment_->AddService<fuchsia::modular::Clipboard>(
      [this](fidl::InterfaceRequest<fuchsia::modular::Clipboard> request) {
        services_from_clipboard_agent_->ConnectToService(
            fuchsia::modular::Clipboard::Name_, request.TakeChannel());
      });
}

void SessionmgrImpl::InitializeMessageQueueManager() {
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

void SessionmgrImpl::InitializeMaxwellAndModular(
    const fidl::StringPtr& session_shell_url,
    fuchsia::modular::AppConfig story_shell) {
  // NOTE: There is an awkward service exchange here between
  // AgentRunner, StoryProviderImpl, FocusHandler, VisibleStoriesHandler.
  //
  // AgentRunner needs a UserIntelligenceProvider to expose services from
  // Maxwell through its GetIntelligenceServices() method.  Initializing the
  // Maxwell process (through UserIntelligenceProviderFactory) requires a
  // ComponentContext. ComponentContext requires an AgentRunner, which creates
  // a circular dependency.
  //
  // Because of FIDL late bindings, we can get around this by creating a new
  // InterfaceRequest here (|intelligence_provider_request|), making the
  // InterfacePtr a valid proxy to be passed to AgentRunner and
  // StoryProviderImpl, even though it won't be bound to a real implementation
  // (provided by Maxwell) until later. It works, but it's not a good pattern.

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

  user_intelligence_provider_impl_.reset(new UserIntelligenceProviderImpl(
      startup_context_, std::move(context_engine),
      [this](fidl::InterfaceRequest<fuchsia::modular::VisibleStoriesProvider>
                 request) {
        visible_stories_handler_->AddProviderBinding(std::move(request));
      },
      [this](fidl::InterfaceRequest<fuchsia::modular::StoryProvider> request) {
        story_provider_impl_->Connect(std::move(request));
      },
      [this](fidl::InterfaceRequest<fuchsia::modular::FocusProvider> request) {
        focus_handler_->AddProviderBinding(std::move(request));
      },
      [this](fidl::InterfaceRequest<fuchsia::modular::PuppetMaster> request) {
        puppet_master_impl_->Connect(std::move(request));
      }));
  AtEnd(Reset(&user_intelligence_provider_impl_));

  entity_provider_runner_ = std::make_unique<EntityProviderRunner>(
      static_cast<EntityProviderLauncher*>(this));
  AtEnd(Reset(&entity_provider_runner_));

  agent_runner_storage_ = std::make_unique<AgentRunnerStorageImpl>(
      ledger_client_.get(), MakePageId(kAgentRunnerPageId));
  AtEnd(Reset(&agent_runner_storage_));

  agent_runner_.reset(new AgentRunner(
      user_environment_->GetLauncher(), message_queue_manager_.get(),
      ledger_repository_.get(), agent_runner_storage_.get(),
      token_provider_factory_.is_bound() ? token_provider_factory_.get()
                                         : nullptr,
      agent_token_manager_.is_bound() ? agent_token_manager_.get() : nullptr,
      user_intelligence_provider_impl_.get(), entity_provider_runner_.get()));
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
            user_environment_->GetLauncher(), std::move(context_engine_config),
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

  user_intelligence_provider_impl_->StartAgents(
      std::move(maxwell_app_component_context),
      fxl::To<fidl::VectorPtr<fidl::StringPtr>>(options_.session_agents),
      fxl::To<fidl::VectorPtr<fidl::StringPtr>>(options_.startup_agents));

  // Setup for kModuleResolverUrl
  {
    module_resolver_ns_services_.AddService<
        fuchsia::modular::IntelligenceServices>(
        [this](fidl::InterfaceRequest<fuchsia::modular::IntelligenceServices>
                   request) {
          fuchsia::modular::ComponentScope component_scope;
          component_scope.set_global_scope(fuchsia::modular::GlobalScope());
          fidl::InterfaceHandle<fuchsia::modular::IntelligenceServices>
              intelligence_services;
          if (user_intelligence_provider_impl_) {
            user_intelligence_provider_impl_->GetComponentIntelligenceServices(
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
    if (options_.test) {
      module_resolver_config.args.push_back("--test");
    }
    // For now, we want data_origin to be "", which uses our (parent process's)
    // /data. This is appropriate for the module_resolver. We can in the future
    // isolate the data it reads to a subdir of /data and map that in here.
    module_resolver_app_ =
        std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
            user_environment_->GetLauncher(), std::move(module_resolver_config),
            "" /* data_origin */, std::move(service_list));
    AtEnd(Reset(&module_resolver_app_));
    AtEnd(Teardown(kBasicTimeout, "Resolver", module_resolver_app_.get()));
  }

  module_resolver_app_->services().ConnectToService(
      module_resolver_service_.NewRequest());
  AtEnd(Reset(&module_resolver_service_));
  // End kModuleResolverUrl

  session_shell_component_context_impl_ =
      std::make_unique<ComponentContextImpl>(
          component_context_info, kSessionShellComponentNamespace,
          session_shell_url, session_shell_url);

  AtEnd(Reset(&session_shell_component_context_impl_));

  fidl::InterfacePtr<fuchsia::modular::FocusProvider>
      focus_provider_story_provider;
  auto focus_provider_request_story_provider =
      focus_provider_story_provider.NewRequest();

  presentation_provider_impl_ =
      std::make_unique<PresentationProviderImpl>(this);
  AtEnd(Reset(&presentation_provider_impl_));

  // We create |story_provider_impl_| after |agent_runner_| so
  // story_provider_impl_ is termiated before agent_runner_, which will cause
  // all modules to be terminated before agents are terminated. Agents must
  // outlive the stories which contain modules that are connected to those
  // agents.
  session_storage_ = std::make_unique<SessionStorage>(
      ledger_client_.get(), fuchsia::ledger::PageId());
  story_provider_impl_.reset(new StoryProviderImpl(
      user_environment_.get(), device_map_impl_->current_device_id(),
      session_storage_.get(), std::move(story_shell), component_context_info,
      std::move(focus_provider_story_provider),
      user_intelligence_provider_impl_.get(), module_resolver_service_.get(),
      entity_provider_runner_.get(), presentation_provider_impl_.get(),
      startup_context_
          ->ConnectToEnvironmentService<fuchsia::ui::viewsv1::ViewSnapshot>(),
      options_.test));
  story_provider_impl_->Connect(std::move(story_provider_request));

  AtEnd(
      Teardown(kStoryProviderTimeout, "StoryProvider", &story_provider_impl_));

  fuchsia::modular::FocusProviderPtr focus_provider_puppet_master;
  auto focus_provider_request_puppet_master =
      focus_provider_puppet_master.NewRequest();
  fuchsia::modular::StoryProviderPtr story_provider_puppet_master;
  auto story_provider_puppet_master_request =
      story_provider_puppet_master.NewRequest();

  // Initialize the PuppetMaster.
  // TODO(miguelfrde): there's no clean runtime interface we can inject to
  // puppet master. Hence, for now we inject this function to be able to focus
  // mods. Eventually we want to have a StoryRuntime and SessionRuntime classes
  // similar to Story/SessionStorage but for runtime management.
  auto module_focuser =
      [story_provider = std::move(story_provider_puppet_master)](
          fidl::StringPtr story_id, fidl::VectorPtr<fidl::StringPtr> mod_name) {
        fuchsia::modular::StoryControllerPtr story_controller;
        story_provider->GetController(story_id, story_controller.NewRequest());

        fuchsia::modular::ModuleControllerPtr module_controller;
        story_controller->GetModuleController(std::move(mod_name),
                                              module_controller.NewRequest());
        module_controller->Focus();
      };
  AtEnd(Reset(&session_storage_));
  story_command_executor_ = MakeProductionStoryCommandExecutor(
      session_storage_.get(), std::move(focus_provider_puppet_master),
      module_resolver_service_.get(), entity_provider_runner_.get(),
      std::move(module_focuser));
  story_provider_impl_->Connect(
      std::move(story_provider_puppet_master_request));
  puppet_master_impl_ = std::make_unique<PuppetMasterImpl>(
      session_storage_.get(), story_command_executor_.get());
  puppet_master_impl_->Connect(std::move(puppet_master_request));

  session_ctl_ =
      std::make_unique<SessionCtl>(startup_context_->outgoing().debug_dir(),
                                   kSessionCtlDir, puppet_master_impl_.get());

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

void SessionmgrImpl::InitializeSessionShell(
    fuchsia::modular::AppConfig session_shell,
    fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
        view_owner_request) {
  // We setup our own view and make the fuchsia::modular::SessionShell a child
  // of it.
  session_shell_view_host_ = std::make_unique<ViewHost>(
      startup_context_
          ->ConnectToEnvironmentService<fuchsia::ui::viewsv1::ViewManager>(),
      std::move(view_owner_request));
  RunSessionShell(std::move(session_shell));
  AtEnd([this](std::function<void()> cont) { TerminateSessionShell(cont); });
}

void SessionmgrImpl::RunSessionShell(
    fuchsia::modular::AppConfig session_shell) {
  // |session_shell_services_| is a ServiceProvider (aka a Directory) that will
  // be used to augment the session shell's namespace.
  session_shell_services_.AddService<fuchsia::modular::SessionShellContext>(
      [this](fidl::InterfaceRequest<fuchsia::modular::SessionShellContext>
                 request) {
        session_shell_context_bindings_.AddBinding(this, std::move(request));
      });
  session_shell_services_.AddService<fuchsia::modular::PuppetMaster>(
      [this](fidl::InterfaceRequest<fuchsia::modular::PuppetMaster> request) {
        puppet_master_impl_->Connect(std::move(request));
      });
  session_shell_services_.AddService<fuchsia::modular::IntelligenceServices>(
      [this](fidl::InterfaceRequest<fuchsia::modular::IntelligenceServices>
                 request) { GetIntelligenceServices(std::move(request)); });
  // |session_shell_service_provider_| is an InterfacePtr impl for
  // ServiceProvider that binds to |session_shell_services_|.
  fuchsia::sys::ServiceProviderPtr session_shell_service_provider_ptr_;
  session_shell_services_.AddBinding(
      session_shell_service_provider_ptr_.NewRequest());

  // |service_list| specifies which services are available to the child
  // component from which ServiceProvicer. There is a lot of indirection here.
  auto service_list = fuchsia::sys::ServiceList::New();
  service_list->names.push_back(fuchsia::modular::SessionShellContext::Name_);
  service_list->names.push_back(fuchsia::modular::PuppetMaster::Name_);
  service_list->provider = std::move(session_shell_service_provider_ptr_);

  session_shell_app_ = std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
      user_environment_->GetLauncher(), std::move(session_shell),
      /* data_origin = */ "", std::move(service_list));

  session_shell_app_->SetAppErrorHandler([this] {
    FXL_LOG(ERROR) << "Session Shell seems to have crashed unexpectedly."
                   << "Logging out.";
    Logout();
  });

  fuchsia::ui::viewsv1token::ViewOwnerPtr view_owner;
  fuchsia::ui::viewsv1::ViewProviderPtr view_provider;
  session_shell_app_->services().ConnectToService(view_provider.NewRequest());
  view_provider->CreateView(view_owner.NewRequest(), nullptr);
  session_shell_view_host_->ConnectView(std::move(view_owner));
}

void SessionmgrImpl::TerminateSessionShell(const std::function<void()>& done) {
  session_shell_app_->Teardown(kBasicTimeout, [this, done] {
    session_shell_app_.reset();
    done();
  });
}

class SessionmgrImpl::SwapSessionShellOperation : public Operation<> {
 public:
  SwapSessionShellOperation(SessionmgrImpl* const sessionmgr_impl,
                            fuchsia::modular::AppConfig session_shell_config,
                            ResultCall result_call)
      : Operation("SessionmgrImpl::SwapSessionShellOperation",
                  std::move(result_call)),
        sessionmgr_impl_(sessionmgr_impl),
        session_shell_config_(std::move(session_shell_config)) {}

 private:
  void Run() override {
    FlowToken flow{this};
    sessionmgr_impl_->story_provider_impl_->StopAllStories([this, flow] {
      sessionmgr_impl_->TerminateSessionShell([this, flow] {
        sessionmgr_impl_->RunSessionShell(std::move(session_shell_config_));
      });
    });
  }

  SessionmgrImpl* const sessionmgr_impl_;
  fuchsia::modular::AppConfig session_shell_config_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SwapSessionShellOperation);
};

void SessionmgrImpl::SwapSessionShell(
    fuchsia::modular::AppConfig session_shell_config,
    SwapSessionShellCallback callback) {
  operation_queue_.Add(new SwapSessionShellOperation(
      this, std::move(session_shell_config), callback));
}

void SessionmgrImpl::Terminate(std::function<void()> done) {
  FXL_LOG(INFO) << "Sessionmgr::Terminate()";
  at_end_done_ = std::move(done);

  TerminateRecurse(at_end_.size() - 1);
}

void SessionmgrImpl::GetAccount(
    std::function<void(::std::unique_ptr<::fuchsia::modular::auth::Account>)>
        callback) {
  callback(fidl::Clone(account_));
}

void SessionmgrImpl::GetAgentProvider(
    fidl::InterfaceRequest<fuchsia::modular::AgentProvider> request) {
  agent_runner_->Connect(std::move(request));
}

void SessionmgrImpl::GetComponentContext(
    fidl::InterfaceRequest<fuchsia::modular::ComponentContext> request) {
  session_shell_component_context_impl_->Connect(std::move(request));
}

void SessionmgrImpl::GetDeviceName(
    std::function<void(::fidl::StringPtr)> callback) {
  callback(device_name_);
}

void SessionmgrImpl::GetFocusController(
    fidl::InterfaceRequest<fuchsia::modular::FocusController> request) {
  focus_handler_->AddControllerBinding(std::move(request));
}

void SessionmgrImpl::GetFocusProvider(
    fidl::InterfaceRequest<fuchsia::modular::FocusProvider> request) {
  focus_handler_->AddProviderBinding(std::move(request));
}

void SessionmgrImpl::GetIntelligenceServices(
    fidl::InterfaceRequest<fuchsia::modular::IntelligenceServices> request) {
  fuchsia::modular::ComponentScope component_scope;
  component_scope.set_global_scope(fuchsia::modular::GlobalScope());
  user_intelligence_provider_impl_->GetComponentIntelligenceServices(
      std::move(component_scope), std::move(request));
}

void SessionmgrImpl::GetLink(
    fidl::InterfaceRequest<fuchsia::modular::Link> request) {
  if (!session_shell_storage_) {
    session_shell_storage_ = std::make_unique<StoryStorage>(
        ledger_client_.get(), fuchsia::ledger::PageId());
  }

  fuchsia::modular::LinkPath link_path;
  link_path.module_path.resize(0);
  link_path.link_name = kSessionShellLinkName;
  auto impl = std::make_unique<LinkImpl>(session_shell_storage_.get(),
                                         std::move(link_path));
  session_shell_link_bindings_.AddBinding(std::move(impl), std::move(request));
}

void SessionmgrImpl::GetPresentation(
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) {
  user_context_->GetPresentation(std::move(request));
}

void SessionmgrImpl::GetSpeechToText(
    fidl::InterfaceRequest<fuchsia::speech::SpeechToText> request) {
  user_intelligence_provider_impl_->GetSpeechToText(std::move(request));
}

void SessionmgrImpl::GetStoryProvider(
    fidl::InterfaceRequest<fuchsia::modular::StoryProvider> request) {
  story_provider_impl_->Connect(std::move(request));
}

void SessionmgrImpl::GetSuggestionProvider(
    fidl::InterfaceRequest<fuchsia::modular::SuggestionProvider> request) {
  user_intelligence_provider_impl_->GetSuggestionProvider(std::move(request));
}

void SessionmgrImpl::GetVisibleStoriesController(
    fidl::InterfaceRequest<fuchsia::modular::VisibleStoriesController>
        request) {
  visible_stories_handler_->AddControllerBinding(std::move(request));
}

void SessionmgrImpl::Logout() { user_context_->Logout(); }

// |EntityProviderLauncher|
void SessionmgrImpl::ConnectToEntityProvider(
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

fuchsia::ledger::cloud::CloudProviderPtr SessionmgrImpl::GetCloudProvider() {
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

void SessionmgrImpl::AtEnd(std::function<void(std::function<void()>)> action) {
  at_end_.emplace_back(std::move(action));
}

void SessionmgrImpl::TerminateRecurse(const int i) {
  if (i >= 0) {
    at_end_[i]([this, i] { TerminateRecurse(i - 1); });
  } else {
    FXL_LOG(INFO) << "Sessionmgr::Terminate(): done";
    at_end_done_();
  }
}

}  // namespace modular
