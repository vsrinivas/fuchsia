// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/user_runner/user_runner_impl.h"

#include <string>

#include "application/lib/app/connect.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/maxwell/services/resolver/resolver.fidl.h"
#include "apps/maxwell/services/suggestion/suggestion_provider.fidl.h"
#include "apps/maxwell/services/user/user_intelligence_provider.fidl.h"
#include "apps/modular/lib/device_info/device_info.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/scope.h"
#include "apps/modular/lib/ledger/storage.h"
#include "apps/modular/services/agent/agent_provider.fidl.h"
#include "apps/modular/services/config/config.fidl.h"
#include "apps/modular/services/story/story_provider.fidl.h"
#include "apps/modular/services/user/user_context.fidl.h"
#include "apps/modular/services/user/user_runner.fidl.h"
#include "apps/modular/services/user/user_shell.fidl.h"
#include "apps/modular/src/component/component_context_impl.h"
#include "apps/modular/src/component/message_queue_manager.h"
#include "apps/modular/src/device_info/device_info_impl.h"
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

}  // namespace

UserRunnerImpl::UserRunnerImpl(
    app::ApplicationEnvironmentPtr application_environment,
    const fidl::String& user_id,
    const fidl::String& device_name,
    AppConfigPtr user_shell,
    AppConfigPtr story_shell,
    fidl::InterfaceHandle<ledger::LedgerRepository> ledger_repository,
    fidl::InterfaceHandle<auth::TokenProviderFactory> token_provider_factory,
    fidl::InterfaceHandle<UserContext> user_context,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    fidl::InterfaceRequest<UserRunner> user_runner_request)
    : binding_(
          new fidl::Binding<UserRunner>(this, std::move(user_runner_request))),
      user_shell_context_binding_(this),
      ledger_repository_(
          ledger::LedgerRepositoryPtr::Create(std::move(ledger_repository))),
      user_scope_(std::move(application_environment),
                  std::string(kUserScopeLabelPrefix) + user_id.data()),
      user_shell_(user_scope_.GetLauncher(), std::move(user_shell)),
      device_name_(device_name) {
  binding_->set_connection_error_handler([this] { Terminate([] {}); });

  // Show user shell.

  mozart::ViewProviderPtr view_provider;
  ConnectToService(user_shell_.services(), view_provider.NewRequest());
  view_provider->CreateView(std::move(view_owner_request), nullptr);

  // Open Ledger.

  ledger_repository_->GetLedger(to_array(kAppId), ledger_.NewRequest(),
                                [](ledger::Status status) {
                                  FTL_CHECK(status == ledger::Status::OK)
                                      << "LedgerRepository.GetLedger() failed: "
                                      << LedgerStatusToString(status);
                                });

  // This must be the first call after GetLedger, otherwise the Ledger
  // starts with one reconciliation strategy, then switches to another.
  ledger_->SetConflictResolverFactory(
      conflict_resolver_.AddBinding(), [](ledger::Status status) {
        if (status != ledger::Status::OK) {
          FTL_LOG(ERROR) << "Ledger.SetConflictResolverFactory() failed: "
                         << LedgerStatusToString(status);
        }
      });

  ledger_->GetRootPage(root_page_.NewRequest(), [](ledger::Status status) {
    if (status != ledger::Status::OK) {
      FTL_LOG(ERROR) << "Ledger.GetRootPage() failed: "
                     << LedgerStatusToString(status);
    }
  });

  // DeviceInfo service
  std::string device_id = LoadDeviceID(user_id);
  std::string device_profile = LoadDeviceProfile();

  device_info_impl_.reset(
      new DeviceInfoImpl(device_name_, device_id, device_profile));
  user_scope_.AddService<DeviceInfo>(
      [this](fidl::InterfaceRequest<DeviceInfo> request) {
        device_info_impl_->Connect(std::move(request));
      });

  // DeviceMap

  device_map_impl_.reset(new DeviceMapImpl(device_name_, device_id,
                                           device_profile, root_page_.get()));
  user_scope_.AddService<DeviceMap>(
      [this](fidl::InterfaceRequest<DeviceMap> request) {
        device_map_impl_->Connect(std::move(request));
      });

  // RemoteInvoker

  // TODO(planders) Do not create RemoteInvoker until service is actually
  // requested.
  remote_invoker_impl_.reset(new RemoteInvokerImpl(ledger_.get()));
  user_scope_.AddService<RemoteInvoker>(
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
  message_queue_path.append(user_id);
  if (!files::CreateDirectory(message_queue_path)) {
    FTL_LOG(FATAL) << "Failed to create message queue directory: "
                   << message_queue_path;
  }
  message_queue_manager_.reset(new MessageQueueManager(
      std::move(message_queue_page), std::move(message_queue_path)));

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

  fidl::InterfaceHandle<FocusProvider> focus_provider;
  auto focus_provider_request = focus_provider.NewRequest();

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

  agent_runner_.reset(new AgentRunner(
      user_scope_.GetLauncher(), message_queue_manager_.get(),
      ledger_repository_.get(), std::move(agent_runner_page),
      std::move(token_provider_factory), user_intelligence_provider_.get()));

  // HACK(anwilson): Start some agents directly by user runner that are needed
  // to keep some dimensions of context updated. They will move
  // elsewhere / become configurable eventually.
  agent_runner_->ConnectToAgent("file:///system/apps/user_runner",
                                "file:///system/apps/agents/home_work_agent",
                                home_work_agent_services_.NewRequest(),
                                home_work_agent_controller_.NewRequest());

  agent_runner_->ConnectToAgent("file:///system/apps/user_runner",
                                "file:///system/apps/agents/walking_agent",
                                walking_agent_services_.NewRequest(),
                                walking_agent_controller_.NewRequest());

  ComponentContextInfo component_context_info{message_queue_manager_.get(),
                                              agent_runner_.get(),
                                              ledger_repository_.get()};

  maxwell_component_context_impl_.reset(new ComponentContextImpl(
      component_context_info, kMaxwellComponentNamespace, kMaxwellUrl,
      kMaxwellUrl));

  maxwell_component_context_binding_.reset(new fidl::Binding<ComponentContext>(
      maxwell_component_context_impl_.get()));

  auto maxwell_config = AppConfig::New();
  maxwell_config->url = kMaxwellUrl;

  maxwell_.reset(
      new AppClientBase(user_scope_.GetLauncher(), std::move(maxwell_config)));

  maxwell::UserIntelligenceProviderFactoryPtr maxwell_factory;
  app::ConnectToService(maxwell_->services(), maxwell_factory.NewRequest());

  maxwell_factory->GetUserIntelligenceProvider(
      maxwell_component_context_binding_->NewBinding(),
      std::move(story_provider), std::move(focus_provider),
      std::move(visible_stories_provider),
      std::move(intelligence_provider_request));

  user_scope_.AddService<resolver::Resolver>(
      std::bind(&maxwell::UserIntelligenceProvider::GetResolver,
                user_intelligence_provider_.get(), std::placeholders::_1));
  // End init maxwell.

  story_provider_impl_.reset(new StoryProviderImpl(
      &user_scope_, device_id, ledger_.get(), root_page_.get(),
      std::move(story_shell), component_context_info,
      user_intelligence_provider_.get()));
  story_provider_impl_->Connect(std::move(story_provider_request));

  focus_handler_.reset(new FocusHandler(device_id, root_page_.get()));
  focus_handler_->AddProviderBinding(std::move(focus_provider_request));

  visible_stories_handler_.reset(new VisibleStoriesHandler);
  visible_stories_handler_->AddProviderBinding(
      std::move(visible_stories_provider_request));

  user_shell_.primary_service()->Initialize(
      std::move(user_context), user_shell_context_binding_.NewBinding());
}

UserRunnerImpl::~UserRunnerImpl() = default;

void UserRunnerImpl::Terminate(const TerminateCallback& done) {
  FTL_LOG(INFO) << "UserRunner::Terminate()";

  user_shell_.AppTerminate([this, done] {
    // We teardown |story_provider_impl_| before |agent_runner_| because the
    // modules running in a story might freak out if agents they are connected
    // to go away while they are still running. On the other hand agents are
    // meant to outlive story lifetimes.
    story_provider_impl_->Teardown([this, done] {
      agent_runner_->Teardown([this, done] {
        // First delete this, then invoke done, finally post stop.
        std::unique_ptr<fidl::Binding<UserRunner>> binding =
            std::move(binding_);
        delete this;
        done();
        mtl::MessageLoop::GetCurrent()->PostQuitTask();

        FTL_LOG(INFO) << "UserRunner::Terminate(): deleted";
      });
    });
  });
}

void UserRunnerImpl::GetDeviceName(const GetDeviceNameCallback& callback) {
  callback(device_name_);
}

void UserRunnerImpl::GetAgentProvider(
    fidl::InterfaceRequest<AgentProvider> request) {
  agent_runner_->Connect(std::move(request));
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

  link_storage_.reset(new StoryStorageImpl(root_page_.get()));
  auto link_path = LinkPath::New();
  link_path->module_path = fidl::Array<fidl::String>::New(0);
  link_path->link_name = kUserShellLinkName;
  user_shell_link_.reset(new LinkImpl(link_storage_.get(), link_path));
  user_shell_link_->Connect(std::move(request));
}

}  // namespace modular
