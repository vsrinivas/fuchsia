// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/user_runner/user_runner_impl.h"

#include <memory>
#include <string>

#include "application/lib/app/connect.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/maxwell/services/resolver/resolver.fidl.h"
#include "apps/maxwell/services/suggestion/suggestion_provider.fidl.h"
#include "apps/maxwell/services/user/user_intelligence_provider.fidl.h"
#include "apps/modular/lib/device_info/device_info.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/scope.h"
#include "apps/modular/lib/ledger/constants.h"
#include "apps/modular/lib/ledger/storage.h"
#include "apps/modular/services/agent/agent_provider.fidl.h"
#include "apps/modular/services/config/config.fidl.h"
#include "apps/modular/services/story/story_provider.fidl.h"
#include "apps/modular/services/user/user_runner.fidl.h"
#include "apps/modular/services/user/user_shell.fidl.h"
#include "apps/modular/src/component/component_context_impl.h"
#include "apps/modular/src/component/message_queue_manager.h"
#include "apps/modular/src/story_runner/link_impl.h"
#include "apps/modular/src/story_runner/story_provider_impl.h"
#include "apps/modular/src/story_runner/story_storage_impl.h"
#include "apps/modular/src/user_runner/device_map_impl.h"
#include "apps/modular/src/user_runner/focus.h"
#include "apps/modular/src/user_runner/remote_invoker_impl.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {

// Template specializations for fidl services that don't have a Terminate()
// method.
template <>
void AppClient<ledger::LedgerRepositoryFactory>::ServiceTerminate(
    const std::function<void()>& done) {
  // The LedgerRepositoryFactory service is not the primary service in the
  // Ledger's executable (LedgerController is primary) and so we shouldn't use
  // this handle to control the lifetime of that executable. Related to FW-265.
  DetachApplicationController();
  ServiceReset();
  done();
}

namespace {

constexpr char kAppId[] = "modular_user_runner";
constexpr char kMaxwellComponentNamespace[] = "maxwell";
constexpr char kMaxwellUrl[] = "file:///system/apps/maxwell";
constexpr char kUserScopeLabelPrefix[] = "user-";
constexpr char kMessageQueuePath[] = "/data/framework/message-queues/v1/";
constexpr char kUserShellLinkName[] = "user-shell-link";

std::string LedgerStatusToString(ledger::Status status) {
  switch (status) {
    case ledger::Status::OK:
      return "OK";
    case ledger::Status::AUTHENTICATION_ERROR:
      return "AUTHENTICATION_ERROR";
    case ledger::Status::PAGE_NOT_FOUND:
      return "PAGE_NOT_FOUND";
    case ledger::Status::KEY_NOT_FOUND:
      return "KEY_NOT_FOUND";
    case ledger::Status::REFERENCE_NOT_FOUND:
      return "REFERENCE_NOT_FOUND";
    case ledger::Status::IO_ERROR:
      return "IO_ERROR";
    case ledger::Status::TRANSACTION_ALREADY_IN_PROGRESS:
      return "TRANSACTION_ALREADY_IN_PROGRESS";
    case ledger::Status::NO_TRANSACTION_IN_PROGRESS:
      return "NO_TRANSACTION_IN_PROGRESS";
    case ledger::Status::INTERNAL_ERROR:
      return "INTERNAL_ERROR";
    case ledger::Status::UNKNOWN_ERROR:
      return "UNKNOWN_ERROR";
    default:
      return "(unknown error)";
  }
};

ledger::FirebaseConfigPtr GetLedgerFirebaseConfig() {
  auto firebase_config = ledger::FirebaseConfig::New();
  firebase_config->server_id = kFirebaseServerId;
  firebase_config->api_key = kFirebaseApiKey;
  return firebase_config;
}

std::string GetLedgerPath(const auth::AccountPtr& account) {
  if (!account.is_null()) {
    return kLedgerDataBaseDir + std::string(account->id);
  }

  // Generate a random number to be used in this case.
  uint32_t random_number;
  size_t random_size;
  mx_status_t status =
      mx_cprng_draw(&random_number, sizeof random_number, &random_size);
  FTL_CHECK(status == MX_OK);
  FTL_CHECK(sizeof random_number == random_size);

  return kLedgerDataBaseDir + std::string("GUEST/") +
         std::to_string(random_number);
}

std::string GetAccountId(const auth::AccountPtr& account) {
  return account.is_null() ? "GUEST" : account->id;
}

}  // namespace

UserRunnerImpl::UserRunnerImpl(
    std::shared_ptr<app::ApplicationContext> const application_context,
    const bool test)
    : binding_(new fidl::Binding<UserRunner>(this)),
      application_context_(application_context),
      test_(test),
      user_shell_context_binding_(this) {
  binding_->set_connection_error_handler([this] { Terminate(); });
}

UserRunnerImpl::~UserRunnerImpl() = default;

void UserRunnerImpl::Connect(fidl::InterfaceRequest<UserRunner> request) {
  binding_->Bind(std::move(request));
}

void UserRunnerImpl::Initialize(
    auth::AccountPtr account, AppConfigPtr user_shell, AppConfigPtr story_shell,
    fidl::InterfaceHandle<auth::TokenProviderFactory> token_provider_factory,
    fidl::InterfaceHandle<UserContext> user_context,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
  token_provider_factory_ =
      auth::TokenProviderFactoryPtr::Create(std::move(token_provider_factory));
  user_context_ = UserContextPtr::Create(std::move(user_context));
  account_ = std::move(account);
  user_scope_ = std::make_unique<Scope>(
      application_context_->environment(),
      std::string(kUserScopeLabelPrefix) + GetAccountId(account));
  user_shell_ = std::make_unique<AppClient<UserShell>>(
      user_scope_->GetLauncher(), std::move(user_shell));

  SetupLedger();

  // Show user shell.
  mozart::ViewProviderPtr view_provider;
  ConnectToService(user_shell_->services(), view_provider.NewRequest());
  view_provider->CreateView(std::move(view_owner_request), nullptr);

  // DeviceMap service
  std::string device_id = LoadDeviceID(GetAccountId(account_));
  device_name_ = LoadDeviceName(GetAccountId(account_));
  std::string device_profile = LoadDeviceProfile();

  device_map_impl_ = std::make_unique<DeviceMapImpl>(
      device_name_, device_id, device_profile, root_page_.get());
  user_scope_->AddService<DeviceMap>(
      [this](fidl::InterfaceRequest<DeviceMap> request) {
        device_map_impl_->Connect(std::move(request));
      });

  // RemoteInvoker

  // TODO(planders) Do not create RemoteInvoker until service is actually
  // requested.
  remote_invoker_impl_ = std::make_unique<RemoteInvokerImpl>(ledger_.get());
  user_scope_->AddService<RemoteInvoker>(
      [this](fidl::InterfaceRequest<RemoteInvoker> request) {
        remote_invoker_impl_->Connect(std::move(request));
      });

  // Setup MessageQueueManager.

  ledger::PagePtr message_queue_page;
  ledger_->GetPage(to_array(kMessageQueuePageId),
                   message_queue_page.NewRequest(), [](ledger::Status status) {
                     if (status != ledger::Status::OK) {
                       FTL_LOG(ERROR)
                           << "Ledger.GetPage(kMessageQueuePageId) failed: "
                           << LedgerStatusToString(status);
                     }
                   });
  std::string message_queue_path = kMessageQueuePath;
  message_queue_path.append(GetAccountId(account));
  if (!files::CreateDirectory(message_queue_path)) {
    FTL_LOG(FATAL) << "Failed to create message queue directory: "
                   << message_queue_path;
  }
  message_queue_manager_ = std::make_unique<MessageQueueManager>(
      std::move(message_queue_page), message_queue_path);

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

  ledger::PagePtr agent_runner_page;
  ledger_->GetPage(to_array(kAgentRunnerPageId), agent_runner_page.NewRequest(),
                   [](ledger::Status status) {
                     if (status != ledger::Status::OK) {
                       FTL_LOG(ERROR)
                           << "Ledger.GetPage(kAgentRunnerPageId) failed: "
                           << LedgerStatusToString(status);
                     }
                   });

  agent_runner_storage_ =
      std::make_unique<AgentRunnerStorageImpl>(std::move(agent_runner_page));

  agent_runner_ = std::make_unique<AgentRunner>(
      user_scope_->GetLauncher(), message_queue_manager_.get(),
      ledger_repository_.get(), agent_runner_storage_.get(),
      token_provider_factory_.get(), user_intelligence_provider_.get());

  ComponentContextInfo component_context_info{message_queue_manager_.get(),
                                              agent_runner_.get(),
                                              ledger_repository_.get()};

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

  maxwell_ = std::make_unique<AppClientBase>(user_scope_->GetLauncher(),
                                             std::move(maxwell_config));

  maxwell::UserIntelligenceProviderFactoryPtr maxwell_factory;
  app::ConnectToService(maxwell_->services(), maxwell_factory.NewRequest());

  maxwell_factory->GetUserIntelligenceProvider(
      maxwell_component_context_binding_->NewBinding(),
      std::move(story_provider), std::move(focus_provider_maxwell),
      std::move(visible_stories_provider),
      std::move(intelligence_provider_request));

  auto component_scope = maxwell::ComponentScope::New();
  component_scope->set_global_scope(maxwell::GlobalScope::New());
  user_intelligence_provider_->GetComponentIntelligenceServices(
      std::move(component_scope), intelligence_services_.NewRequest());

  user_scope_->AddService<resolver::Resolver>(
      std::bind(&maxwell::UserIntelligenceProvider::GetResolver,
                user_intelligence_provider_.get(), std::placeholders::_1));
  // End init maxwell.

  fidl::InterfacePtr<FocusProvider> focus_provider_story_provider;
  auto focus_provider_request_story_provider =
      focus_provider_story_provider.NewRequest();

  story_provider_impl_ = std::make_unique<StoryProviderImpl>(
      user_scope_.get(), device_id, ledger_.get(), root_page_.get(),
      std::move(story_shell), component_context_info,
      std::move(focus_provider_story_provider), intelligence_services_.get(),
      user_intelligence_provider_.get());
  story_provider_impl_->Connect(std::move(story_provider_request));

  focus_handler_ = std::make_unique<FocusHandler>(device_id, root_page_.get());
  focus_handler_->AddProviderBinding(std::move(focus_provider_request_maxwell));
  focus_handler_->AddProviderBinding(
      std::move(focus_provider_request_story_provider));

  visible_stories_handler_ = std::make_unique<VisibleStoriesHandler>();
  visible_stories_handler_->AddProviderBinding(
      std::move(visible_stories_provider_request));

  user_shell_->primary_service()->Initialize(
      user_shell_context_binding_.NewBinding());
}

void UserRunnerImpl::Terminate() {
  FTL_LOG(INFO) << "UserRunner::Terminate()";

  user_shell_->AppTerminate([this] {
    // We teardown |story_provider_impl_| before |agent_runner_| because the
    // modules running in a story might freak out if agents they are connected
    // to go away while they are still running. On the other hand agents are
    // meant to outlive story lifetimes.
    story_provider_impl_->Teardown([this] {
      agent_runner_->Teardown([this] {
        ledger_app_client_->AppTerminate([this] {
          FTL_LOG(INFO) << "UserRunner::Terminate(): done";
          mtl::MessageLoop::GetCurrent()->QuitNow();
        });
      });
    });
  });
}

void UserRunnerImpl::GetAccount(const GetAccountCallback& callback) {
  callback(account_.Clone());
}

void UserRunnerImpl::GetAgentProvider(
    fidl::InterfaceRequest<AgentProvider> request) {
  agent_runner_->Connect(std::move(request));
}

void UserRunnerImpl::GetContextReader(
    fidl::InterfaceRequest<maxwell::ContextReader> request) {
  intelligence_services_->GetContextReader(std::move(request));
}

void UserRunnerImpl::GetContextPublisher(
    fidl::InterfaceRequest<maxwell::ContextPublisher> request) {
  intelligence_services_->GetContextPublisher(std::move(request));
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

void UserRunnerImpl::GetLink(fidl::InterfaceRequest<Link> request) {
  if (user_shell_link_) {
    user_shell_link_->Connect(std::move(request));
    return;
  }

  link_storage_ = std::make_unique<StoryStorageImpl>(root_page_.get());
  LinkPathPtr link_path = LinkPath::New();
  link_path->module_path = fidl::Array<fidl::String>::New(0);
  link_path->link_name = kUserShellLinkName;
  user_shell_link_ = std::make_unique<LinkImpl>(link_storage_.get(), std::move(link_path));
  user_shell_link_->Connect(std::move(request));
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

void UserRunnerImpl::Logout() { user_context_->Logout(); }

void UserRunnerImpl::LogoutAndResetLedgerState() {
  fidl::InterfaceHandle<auth::TokenProvider> ledger_token_provider_for_erase;
  token_provider_factory_->GetTokenProvider(
      kLedgerAppUrl, ledger_token_provider_for_erase.NewRequest());
  auto firebase_config = GetLedgerFirebaseConfig();
  ledger_app_client_->primary_service()->EraseRepository(
      GetLedgerPath(account_), std::move(firebase_config),
      std::move(ledger_token_provider_for_erase),
      [this](ledger::Status status) {
        if (status != ledger::Status::OK) {
          FTL_LOG(ERROR) << "EraseRepository failed: " << status
                         << "Logging out.";
        }
        user_context_->Logout();
      });
}

void UserRunnerImpl::SetupLedger() {
  // Start the ledger.
  AppConfigPtr ledger_config = AppConfig::New();
  ledger_config->url = kLedgerAppUrl;
  ledger_config->args = fidl::Array<fidl::String>::New(1);
  ledger_config->args[0] = kLedgerNoMinfsWaitFlag;
  ledger_app_client_ =
      std::make_unique<AppClient<ledger::LedgerRepositoryFactory>>(
          user_scope_->GetLauncher(), std::move(ledger_config));
  ledger_app_client_->SetAppErrorHandler([this] {
    FTL_LOG(ERROR) << "Ledger seems to have crashed unexpectedly." << std::endl
                   << "CALLING Logout() DUE TO UNRECOVERABLE LEDGER ERROR.";
    Logout();
  });

  // Get a token provider instance to pass to ledger.
  fidl::InterfaceHandle<auth::TokenProvider> ledger_token_provider;
  token_provider_factory_->GetTokenProvider(kLedgerAppUrl,
                                            ledger_token_provider.NewRequest());

  ledger::FirebaseConfigPtr firebase_config;
  if (account_) {
    firebase_config = GetLedgerFirebaseConfig();
  }

  ledger_app_client_->primary_service()->GetRepository(
      GetLedgerPath(account_), std::move(firebase_config),
      std::move(ledger_token_provider), ledger_repository_.NewRequest(),
      [this](ledger::Status status) {
        if (status != ledger::Status::OK) {
          FTL_LOG(ERROR)
              << "LedgerRepositoryFactory.GetRepository() failed: "
              << LedgerStatusToString(status) << std::endl
              << "CALLING Logout() DUE TO UNRECOVERABLE LEDGER ERROR.";
          Logout();
        }
      });

  // If ledger state is erased from underneath us (happens when the cloud store
  // is cleared), ledger will close the connection to |ledger_repository_|.
  ledger_repository_.set_connection_error_handler([this] { Logout(); });

  // Open Ledger.
  ledger_repository_->GetLedger(
      to_array(kAppId), ledger_.NewRequest(), [this](ledger::Status status) {
        if (status != ledger::Status::OK) {
          FTL_LOG(ERROR)
              << "LedgerRepository.GetLedger() failed: "
              << LedgerStatusToString(status) << std::endl
              << "CALLING Logout() DUE TO UNRECOVERABLE LEDGER ERROR.";
          Logout();
        }
      });

  // This must be the first call after GetLedger, otherwise the Ledger
  // starts with one reconciliation strategy, then switches to another.
  ledger_->SetConflictResolverFactory(
      conflict_resolver_.AddBinding(), [this](ledger::Status status) {
        if (status != ledger::Status::OK) {
          FTL_LOG(ERROR)
              << "Ledger.SetConflictResolverFactory() failed: "
              << LedgerStatusToString(status) << std::endl
              << "CALLING Logout() DUE TO UNRECOVERABLE LEDGER ERROR.";
          Logout();
        }
      });

  ledger_->GetRootPage(root_page_.NewRequest(), [this](ledger::Status status) {
    if (status != ledger::Status::OK) {
      FTL_LOG(ERROR) << "Ledger.GetRootPage() failed: "
                     << LedgerStatusToString(status) << std::endl
                     << "CALLING Logout() DUE TO UNRECOVERABLE LEDGER ERROR.";
      Logout();
    }
  });
}

}  // namespace modular
