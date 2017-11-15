// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/user_runner_impl.h"

#include <memory>
#include <string>

#include "lib/agent/fidl/agent_provider.fidl.h"
#include "lib/app/cpp/connect.h"
#include "lib/config/fidl/config.fidl.h"
#include "lib/fxl/files/directory.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/ledger/fidl/ledger.fidl.h"
#include "lib/network/fidl/network_service.fidl.h"
#include "lib/resolver/fidl/resolver.fidl.h"
#include "lib/story/fidl/story_provider.fidl.h"
#include "lib/suggestion/fidl/suggestion_provider.fidl.h"
#include "lib/ui/views/fidl/view_provider.fidl.h"
#include "lib/ui/views/fidl/view_token.fidl.h"
#include "lib/user/fidl/user_runner.fidl.h"
#include "lib/user/fidl/user_shell.fidl.h"
#include "lib/user_intelligence/fidl/user_intelligence_provider.fidl.h"
#include "peridot/bin/cloud_provider_firebase/fidl/factory.fidl.h"
#include "peridot/bin/component/component_context_impl.h"
#include "peridot/bin/component/message_queue_manager.h"
#include "peridot/bin/ledger/fidl/debug.fidl.h"
#include "peridot/bin/story_runner/link_impl.h"
#include "peridot/bin/story_runner/story_provider_impl.h"
#include "peridot/bin/user_runner/device_map_impl.h"
#include "peridot/bin/user_runner/focus.h"
#include "peridot/bin/user_runner/remote_invoker_impl.h"
#include "peridot/lib/common/teardown.h"
#include "peridot/lib/common/xdr.h"
#include "peridot/lib/device_info/device_info.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "peridot/lib/fidl/scope.h"
#include "peridot/lib/ledger_client/constants.h"
#include "peridot/lib/ledger_client/ledger_client.h"
#include "peridot/lib/ledger_client/status.h"
#include "peridot/lib/ledger_client/storage.h"

namespace modular {

// Maxwell doesn't yet implement lifecycle or has a lifecycle method, so we just
// let AppClient close the controller connection immediately. (The controller
// connection is closed once the ServiceTerminate() call invokes its done
// callback.)
template <>
void AppClient<maxwell::UserIntelligenceProviderFactory>::ServiceTerminate(
    const std::function<void()>& done) {
  done();
}

namespace {

constexpr char kAppId[] = "modular_user_runner";
constexpr char kMaxwellComponentNamespace[] = "maxwell";
constexpr char kMaxwellUrl[] = "maxwell";
constexpr char kModuleResolverUrl[] = "module_resolver";
constexpr char kUserScopeLabelPrefix[] = "user-";
constexpr char kMessageQueuePath[] = "/data/MESSAGE_QUEUES/v1/";
constexpr char kUserShellComponentNamespace[] = "user-shell-namespace";
constexpr char kUserShellLinkName[] = "user-shell-link";
constexpr char kLedgerDashboardUrl[] =
    "file:///system/apps/ledger_dashboard.dartx";
constexpr char kLedgerDashboardEnvLabel[] = "ledger-dashboard";

cloud_provider_firebase::ConfigPtr GetLedgerFirebaseConfig() {
  auto firebase_config = cloud_provider_firebase::Config::New();
  firebase_config->server_id = kFirebaseServerId;
  firebase_config->api_key = kFirebaseApiKey;
  return firebase_config;
}

std::string GetAccountId(const auth::AccountPtr& account) {
  return account.is_null() ? "GUEST" : account->id;
}

}  // namespace

UserRunnerImpl::UserRunnerImpl(
    app::ApplicationContext* const application_context,
    const bool test)
    : application_context_(application_context),
      test_(test),
      user_shell_context_binding_(this),
      story_provider_impl_("StoryProviderImpl"),
      agent_runner_("AgentRunner") {
  application_context_->outgoing_services()->AddService<UserRunner>(
      [this](fidl::InterfaceRequest<UserRunner> request) {
        bindings_.AddBinding(this, std::move(request));
      });

  // TODO(alhaad): Once VFS supports asynchronous operations, expose directly
  // to filesystem instead of this indirection.
  application_context_->outgoing_services()->AddService<UserRunnerDebug>(
      [this](fidl::InterfaceRequest<UserRunnerDebug> request) {
        user_runner_debug_bindings_.AddBinding(this, std::move(request));
      });
}

UserRunnerImpl::~UserRunnerImpl() = default;

void UserRunnerImpl::Initialize(
    auth::AccountPtr account,
    AppConfigPtr user_shell,
    AppConfigPtr story_shell,
    fidl::InterfaceHandle<auth::TokenProviderFactory> token_provider_factory,
    fidl::InterfaceHandle<UserContext> user_context,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
  token_provider_factory_ =
      auth::TokenProviderFactoryPtr::Create(std::move(token_provider_factory));
  user_context_ = UserContextPtr::Create(std::move(user_context));
  account_ = std::move(account);
  user_scope_ = std::make_unique<Scope>(
      application_context_->environment(),
      std::string(kUserScopeLabelPrefix) + GetAccountId(account_));
  user_shell_app_ = std::make_unique<AppClient<Lifecycle>>(
      user_scope_->GetLauncher(), user_shell.Clone());
  ConnectToService(user_shell_app_->services(), user_shell_.NewRequest());
  user_shell_app_->SetAppErrorHandler([this] {
    FXL_LOG(ERROR) << "User Shell seems to have crashed unexpectedly."
                   << "Logging out.";
    Logout();
  });

  SetupLedger();
  StartLedgerDashboard();

  // Show user shell.
  mozart::ViewProviderPtr view_provider;
  ConnectToService(user_shell_app_->services(), view_provider.NewRequest());
  view_provider->CreateView(std::move(view_owner_request), nullptr);

  // DeviceMap service
  const std::string device_id = LoadDeviceID(GetAccountId(account_));
  device_name_ = LoadDeviceName(GetAccountId(account_));
  const std::string device_profile = LoadDeviceProfile();

  device_map_impl_ = std::make_unique<DeviceMapImpl>(
      device_name_, device_id, device_profile, ledger_client_.get(),
      fidl::Array<uint8_t>::New(16));
  user_scope_->AddService<DeviceMap>(
      [this](fidl::InterfaceRequest<DeviceMap> request) {
        if (device_map_impl_) {
          device_map_impl_->Connect(std::move(request));
        }
      });

  // RemoteInvoker

  // TODO(planders) Do not create RemoteInvoker until service is actually
  // requested.
  remote_invoker_impl_ =
      std::make_unique<RemoteInvokerImpl>(ledger_client_->ledger());
  user_scope_->AddService<RemoteInvoker>(
      [this](fidl::InterfaceRequest<RemoteInvoker> request) {
        if (remote_invoker_impl_) {
          remote_invoker_impl_->Connect(std::move(request));
        }
      });

  // Setup MessageQueueManager.

  std::string message_queue_path = kMessageQueuePath;
  message_queue_path.append(GetAccountId(account_));
  if (!files::CreateDirectory(message_queue_path)) {
    FXL_LOG(FATAL) << "Failed to create message queue directory: "
                   << message_queue_path;
  }

  message_queue_manager_ = std::make_unique<MessageQueueManager>(
      ledger_client_.get(), to_array(kMessageQueuePageId), message_queue_path);

  // Begin init maxwell.
  //
  // NOTE: There is an awkward service exchange here between
  // UserIntelligenceProvider, AgentRunner, StoryProviderImpl,
  // FocusHandler, VisibleStoriesHandler.
  //
  // AgentRunner needs a UserIntelligenceProvider to expose services
  // from Maxwell through its GetIntelligenceServices() method.
  // Initializing the Maxwell process (through
  // UserIntelligenceProviderFactory) requires a ComponentContext.
  // ComponentContext requires an AgentRunner, which creates a
  // circular dependency.
  //
  // Because of FIDL late bindings, we can get around this by creating
  // a new InterfaceRequest here (|intelligence_provider_request|),
  // making the InterfacePtr a valid proxy to be passed to AgentRunner
  // and StoryProviderImpl, even though it won't be bound to a real
  // implementation (provided by Maxwell) until later. It works, but
  // it's not a good pattern.
  //
  // A similar relationship holds between FocusHandler and
  // UserIntelligenceProvider.
  auto intelligence_provider_request = user_intelligence_provider_.NewRequest();

  fidl::InterfaceHandle<StoryProvider> story_provider;
  auto story_provider_request = story_provider.NewRequest();

  fidl::InterfaceHandle<FocusProvider> focus_provider_maxwell;
  auto focus_provider_request_maxwell = focus_provider_maxwell.NewRequest();

  fidl::InterfaceHandle<VisibleStoriesProvider> visible_stories_provider;
  auto visible_stories_provider_request = visible_stories_provider.NewRequest();

  entity_provider_runner_ = std::make_unique<EntityProviderRunner>(
      static_cast<EntityProviderLauncher*>(this));

  agent_runner_storage_ = std::make_unique<AgentRunnerStorageImpl>(
      ledger_client_.get(), to_array(kAgentRunnerPageId));

  agent_runner_.reset(new AgentRunner(
      user_scope_->GetLauncher(), message_queue_manager_.get(),
      ledger_repository_.get(), agent_runner_storage_.get(),
      token_provider_factory_.get(), user_intelligence_provider_.get(),
      entity_provider_runner_.get()));

  ComponentContextInfo component_context_info{
      message_queue_manager_.get(), agent_runner_.get(),
      ledger_repository_.get(), entity_provider_runner_.get()};

  maxwell_component_context_impl_ = std::make_unique<ComponentContextImpl>(
      component_context_info, kMaxwellComponentNamespace, kMaxwellUrl,
      kMaxwellUrl);

  maxwell_component_context_binding_ =
      std::make_unique<fidl::Binding<ComponentContext>>(
          maxwell_component_context_impl_.get());

  auto maxwell_config = AppConfig::New();
  maxwell_config->url = kMaxwellUrl;
  if (test_) {
    maxwell_config->args.push_back(
        "--config=/system/data/maxwell/test_config.json");
  }

  maxwell_ =
      std::make_unique<AppClient<maxwell::UserIntelligenceProviderFactory>>(
          user_scope_->GetLauncher(), std::move(maxwell_config));

  maxwell_->primary_service()->GetUserIntelligenceProvider(
      maxwell_component_context_binding_->NewBinding(),
      std::move(story_provider), std::move(focus_provider_maxwell),
      std::move(visible_stories_provider),
      std::move(intelligence_provider_request));

  auto component_scope = maxwell::ComponentScope::New();
  component_scope->set_global_scope(maxwell::GlobalScope::New());
  user_intelligence_provider_->GetComponentIntelligenceServices(
      std::move(component_scope), intelligence_services_.NewRequest());

  user_scope_->AddService<resolver::Resolver>(
      [this](fidl::InterfaceRequest<resolver::Resolver> request) {
        if (user_intelligence_provider_) {
          user_intelligence_provider_->GetResolver(std::move(request));
        }
      });

  // Setup for kModuleResolverUrl
  module_resolver_ns_services_.AddService<maxwell::IntelligenceServices>(
      [this](fidl::InterfaceRequest<maxwell::IntelligenceServices> request) {
        auto component_scope = maxwell::ComponentScope::New();
        component_scope->set_global_scope(maxwell::GlobalScope::New());
        fidl::InterfaceHandle<maxwell::IntelligenceServices>
            intelligence_services;
        user_intelligence_provider_->GetComponentIntelligenceServices(
            std::move(component_scope), std::move(request));

      });
  auto service_list = app::ServiceList::New();
  service_list->names.push_back(maxwell::IntelligenceServices::Name_);
  module_resolver_ns_services_.AddBinding(service_list->provider.NewRequest());

  auto module_resolver_config = AppConfig::New();
  module_resolver_config->url = kModuleResolverUrl;
  // For now, we want data_origin to be "", which uses our (parent process's)
  // /data. This is appropriate for the module_resolver, for now. We can in the
  // future isolate the data it reads to a subdir of /data and map that in
  // here.
  module_resolver_ = std::make_unique<AppClient<Lifecycle>>(
      user_scope_->GetLauncher(), std::move(module_resolver_config),
      "" /* data_origin */, std::move(service_list));
  ConnectToService(module_resolver_->services(),
                   module_resolver_service_.NewRequest());
  // End kModuleResolverUrl
  // End init maxwell.

  user_shell_component_context_impl_ = std::make_unique<ComponentContextImpl>(
      component_context_info, kUserShellComponentNamespace, user_shell->url,
      user_shell->url);

  fidl::InterfacePtr<FocusProvider> focus_provider_story_provider;
  auto focus_provider_request_story_provider =
      focus_provider_story_provider.NewRequest();

  story_provider_impl_.reset(new StoryProviderImpl(
      user_scope_.get(), device_id, ledger_client_.get(),
      fidl::Array<uint8_t>::New(16), std::move(story_shell),
      component_context_info, std::move(focus_provider_story_provider),
      intelligence_services_.get(), user_intelligence_provider_.get(),
      module_resolver_service_.get()));
  story_provider_impl_->Connect(std::move(story_provider_request));

  focus_handler_ = std::make_unique<FocusHandler>(
      device_id, ledger_client_.get(), fidl::Array<uint8_t>::New(16));
  focus_handler_->AddProviderBinding(std::move(focus_provider_request_maxwell));
  focus_handler_->AddProviderBinding(
      std::move(focus_provider_request_story_provider));

  visible_stories_handler_ = std::make_unique<VisibleStoriesHandler>();
  visible_stories_handler_->AddProviderBinding(
      std::move(visible_stories_provider_request));

  user_shell_->Initialize(user_shell_context_binding_.NewBinding());
}

void UserRunnerImpl::Terminate(std::function<void()> done) {
  FXL_LOG(INFO) << "UserRunner::Terminate()";

  // We need to Terminate() every member that has life cycle here. In addition,
  // everything that has fidl connections to something that is told to
  // Terminate(), if it doesn't have a Terminate() on its own, must be reset()
  // before the thing it connects to receives Terminate(). Specifically, all
  // PageClient instances and the LedgerClient instance must be reset() before
  // the ledger app is AppTerminate()d.
  //
  // TODO(mesch,alhaad): It would be nice if this dependency relationship graph
  // would be created implicitly and automatically from the initialization
  // sequence (which must reflect the same dependency semi-ordering in reverse)
  // executed in Initialize(). I.e. it would be nice if we had the asynchronous
  // analogue of what constructor/destructor order aligmnent does for
  // synchronous initialization and termination.
  //
  // A few ideas in that direction:
  //
  // * Several members are kept here but used only by other members. Ownership
  //   should be transferred accordingly.
  //
  // * We could use more structured holders than unqiue_ptr<>s which also hold
  //   dependent pointers, and the asynchronously generically traverse the
  //   resulting graph.
  //
  // This list is the reverse from the initialization order executed in
  // Initialize() above.
  user_shell_app_->Teardown(kBasicTimeout, [this, done] {
    FXL_DLOG(INFO) << "- UserShell down";
    user_shell_.reset();
    user_shell_app_.reset();

    visible_stories_handler_.reset();
    focus_handler_.reset();

    // We teardown |story_provider_impl_| before |agent_runner_| because the
    // modules running in a story might freak out if agents they are connected
    // to go away while they are still running. On the other hand agents are
    // meant to outlive story lifetimes.
    story_provider_impl_.Teardown(kStoryProviderTimeout, [this, done] {
      FXL_DLOG(INFO) << "- StoryProvider down";

      user_intelligence_provider_.reset();
      maxwell_->Teardown(kBasicTimeout, [this, done] {
        module_resolver_->Teardown(kBasicTimeout, [this, done] {
          FXL_DLOG(INFO) << "- Maxwell down";
          maxwell_.reset();

          maxwell_component_context_binding_.reset();
          maxwell_component_context_impl_.reset();

          agent_runner_.Teardown(kAgentRunnerTimeout, [this, done] {
            FXL_DLOG(INFO) << "- AgentRunner down";
            agent_runner_storage_.reset();

            entity_provider_runner_.reset();
            message_queue_manager_.reset();
            remote_invoker_impl_.reset();
            device_map_impl_.reset();

            ledger_dashboard_client_->Teardown(kBasicTimeout, [this, done] {
              ledger_dashboard_client_.reset();
              ledger_dashboard_scope_.reset();

              ledger_client_.reset();
              ledger_repository_.reset();
              ledger_repository_factory_.reset();

              ledger_app_client_->Teardown(kBasicTimeout, [this, done] {
                FXL_DLOG(INFO) << "- Ledger down";
                ledger_app_client_.reset();

                user_scope_.reset();
                account_.reset();
                user_context_.reset();
                token_provider_factory_.reset();

                FXL_LOG(INFO) << "UserRunner::Terminate(): done";
                done();
              });
            });
          });
        });
      });
    });
  });
}

void UserRunnerImpl::DumpState(const DumpStateCallback& callback) {
  std::ostringstream output;
  output << "=================Begin user info====================" << std::endl;

  output << "=================Begin account info=================" << std::endl;
  std::string account_json;
  XdrWrite(&account_json, &account_, XdrAccount);
  output << account_json << std::endl;

  story_provider_impl_->DumpState(fxl::MakeCopyable(
      [output = std::move(output), callback](const std::string& debug) mutable {
        output << debug;
        callback(output.str());
      }));

  // TODO(alhaad): Add debug info about agents, device map, etc.
}

void UserRunnerImpl::GetAccount(const GetAccountCallback& callback) {
  callback(account_.Clone());
}

void UserRunnerImpl::GetAgentProvider(
    fidl::InterfaceRequest<AgentProvider> request) {
  agent_runner_->Connect(std::move(request));
}

void UserRunnerImpl::GetComponentContext(
    fidl::InterfaceRequest<ComponentContext> request) {
  user_shell_component_context_bindings_.AddBinding(
      user_shell_component_context_impl_.get(), std::move(request));
}

void UserRunnerImpl::GetContextReader(
    fidl::InterfaceRequest<maxwell::ContextReader> request) {
  intelligence_services_->GetContextReader(std::move(request));
}

void UserRunnerImpl::GetContextWriter(
    fidl::InterfaceRequest<maxwell::ContextWriter> request) {
  intelligence_services_->GetContextWriter(std::move(request));
}

void UserRunnerImpl::GetDeviceName(const GetDeviceNameCallback& callback) {
  callback(device_name_);
}

void UserRunnerImpl::GetFocusController(
    fidl::InterfaceRequest<FocusController> request) {
  focus_handler_->AddControllerBinding(std::move(request));
}

void UserRunnerImpl::GetFocusProvider(
    fidl::InterfaceRequest<FocusProvider> request) {
  focus_handler_->AddProviderBinding(std::move(request));
}

void UserRunnerImpl::GetIntelligenceServices(
    fidl::InterfaceRequest<maxwell::IntelligenceServices> request) {
  auto component_scope = maxwell::ComponentScope::New();
  component_scope->set_global_scope(maxwell::GlobalScope::New());
  user_intelligence_provider_->GetComponentIntelligenceServices(
      std::move(component_scope), std::move(request));
}

void UserRunnerImpl::GetLink(fidl::InterfaceRequest<Link> request) {
  if (user_shell_link_) {
    user_shell_link_->Connect(std::move(request));
    return;
  }

  LinkPathPtr link_path = LinkPath::New();
  link_path->module_path = fidl::Array<fidl::String>::New(0);
  link_path->link_name = kUserShellLinkName;
  user_shell_link_ = std::make_unique<LinkImpl>(ledger_client_.get(),
                                                fidl::Array<uint8_t>::New(16),
                                                std::move(link_path));
  user_shell_link_->Connect(std::move(request));
}

void UserRunnerImpl::GetPresentation(
    fidl::InterfaceRequest<mozart::Presentation> request) {
  user_context_->GetPresentation(std::move(request));
}

void UserRunnerImpl::GetProposalPublisher(
    fidl::InterfaceRequest<maxwell::ProposalPublisher> request) {
  intelligence_services_->GetProposalPublisher(std::move(request));
}

void UserRunnerImpl::GetStoryProvider(
    fidl::InterfaceRequest<StoryProvider> request) {
  story_provider_impl_->Connect(std::move(request));
}

void UserRunnerImpl::GetSuggestionProvider(
    fidl::InterfaceRequest<maxwell::SuggestionProvider> request) {
  user_intelligence_provider_->GetSuggestionProvider(std::move(request));
}

void UserRunnerImpl::GetVisibleStoriesController(
    fidl::InterfaceRequest<VisibleStoriesController> request) {
  visible_stories_handler_->AddControllerBinding(std::move(request));
}

void UserRunnerImpl::Logout() {
  user_context_->Logout();
}

// |EntityProviderLauncher|
void UserRunnerImpl::ConnectToEntityProvider(
    const std::string& agent_url,
    fidl::InterfaceRequest<EntityProvider> entity_provider_request,
    fidl::InterfaceRequest<AgentController> agent_controller_request) {
  FXL_DCHECK(agent_runner_.get());
  agent_runner_->ConnectToEntityProvider(agent_url,
                                         std::move(entity_provider_request),
                                         std::move(agent_controller_request));
}

void UserRunnerImpl::SetupLedger() {
  // Start the ledger.
  AppConfigPtr ledger_config = AppConfig::New();
  ledger_config->url = kLedgerAppUrl;
  ledger_config->args = fidl::Array<fidl::String>::New(1);
  ledger_config->args[0] = kLedgerNoMinfsWaitFlag;
  ledger_app_client_ = std::make_unique<AppClient<ledger::LedgerController>>(
      user_scope_->GetLauncher(), std::move(ledger_config), "/data/LEDGER");
  ledger_app_client_->SetAppErrorHandler([this] {
    FXL_LOG(ERROR) << "Ledger seems to have crashed unexpectedly." << std::endl
                   << "CALLING Logout() DUE TO UNRECOVERABLE LEDGER ERROR.";
    Logout();
  });

  cloud_provider::CloudProviderPtr cloud_provider;
  if (account_) {
    // If not running in Guest mode, spin up a cloud provider for Ledger to use
    // for syncing.
    AppConfigPtr cloud_provider_config = AppConfig::New();
    cloud_provider_config->url = kCloudProviderFirebaseAppUrl;
    cloud_provider_config->args = fidl::Array<fidl::String>::New(0);
    cloud_provider_client_ = std::make_unique<AppClient<Lifecycle>>(
        user_scope_->GetLauncher(), std::move(cloud_provider_config));
    ConnectToService(cloud_provider_client_->services(),
                     cloud_provider_factory_.NewRequest());

    cloud_provider = GetCloudProvider();
  }

  ConnectToService(ledger_app_client_->services(),
                   ledger_repository_factory_.NewRequest());

  // The directory "/data" is the data root "/data/LEDGER" that the ledger app
  // client is configured to.
  ledger_repository_factory_->GetRepository(
      "/data", std::move(cloud_provider), ledger_repository_.NewRequest(),
      [this](ledger::Status status) {
        if (status != ledger::Status::OK) {
          FXL_LOG(ERROR)
              << "LedgerRepositoryFactory.GetRepository() failed: "
              << LedgerStatusToString(status) << std::endl
              << "CALLING Logout() DUE TO UNRECOVERABLE LEDGER ERROR.";
          Logout();
        }
      });

  // If ledger state is erased from underneath us (happens when the cloud store
  // is cleared), ledger will close the connection to |ledger_repository_|.
  ledger_repository_.set_connection_error_handler([this] { Logout(); });

  ledger_client_.reset(
      new LedgerClient(ledger_repository_.get(), kAppId, [this] {
        FXL_LOG(ERROR) << "CALLING Logout() DUE TO UNRECOVERABLE LEDGER ERROR.";
        Logout();
      }));
}

cloud_provider::CloudProviderPtr UserRunnerImpl::GetCloudProvider() {
  cloud_provider::CloudProviderPtr cloud_provider;
  fidl::InterfaceHandle<auth::TokenProvider> ledger_token_provider;
  token_provider_factory_->GetTokenProvider(kLedgerAppUrl,
                                            ledger_token_provider.NewRequest());
  auto firebase_config = GetLedgerFirebaseConfig();

  cloud_provider_factory_->GetCloudProvider(
      std::move(firebase_config), std::move(ledger_token_provider),
      cloud_provider.NewRequest(), [](cloud_provider::Status status) {
        if (status != cloud_provider::Status::OK) {
          FXL_LOG(ERROR) << "Failed to create a cloud provider: " << status;
        }
      });
  return cloud_provider;
}

void UserRunnerImpl::StartLedgerDashboard() {
  ledger_dashboard_scope_ = std::make_unique<Scope>(
      user_scope_->environment(), std::string(kLedgerDashboardEnvLabel));

  ledger_dashboard_scope_->AddService<ledger::LedgerRepositoryDebug>(
      [this](fidl::InterfaceRequest<ledger::LedgerRepositoryDebug> request) {
        ledger_repository_->GetLedgerRepositoryDebug(
            std::move(request), [](ledger::Status status) {
              if (status != ledger::Status::OK) {
                FXL_LOG(ERROR)
                    << "LedgerRepository.GetLedgerRepositoryDebug() failed: "
                    << LedgerStatusToString(status);
              }
            });
      });

  auto ledger_dashboard_config = AppConfig::New();
  ledger_dashboard_config->url = kLedgerDashboardUrl;
  ledger_dashboard_client_ = std::make_unique<AppClient<Lifecycle>>(
      ledger_dashboard_scope_->GetLauncher(),
      std::move(ledger_dashboard_config));
  FXL_LOG(INFO) << "Starting Ledger dashboard " << kLedgerDashboardUrl;
}

}  // namespace modular
