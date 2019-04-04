// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/sessionmgr_impl.h"

#include <fcntl.h>
#include <fuchsia/ledger/cloud/firestore/cpp/fidl.h>
#include <fuchsia/ledger/cpp/fidl.h>
#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <lib/component/cpp/connect.h>
#include <lib/fsl/io/fd.h>
#include <lib/fsl/types/type_converters.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/eventpair.h>
#include <src/lib/files/directory.h>
#include <src/lib/files/unique_fd.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/macros.h>
#include <memory>
#include <string>

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
#include "peridot/lib/common/teardown.h"
#include "peridot/lib/device_info/device_info.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/environment.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "peridot/lib/ledger_client/constants.h"
#include "peridot/lib/ledger_client/ledger_client.h"
#include "peridot/lib/ledger_client/page_id.h"
#include "peridot/lib/ledger_client/status.h"
#include "peridot/lib/module_manifest/module_facet_reader_impl.h"

namespace modular {

namespace {

constexpr char kAppId[] = "modular_sessionmgr";

constexpr char kMaxwellComponentNamespace[] = "maxwell";
constexpr char kMaxwellUrl[] = "maxwell";

constexpr char kContextEngineUrl[] =
    "fuchsia-pkg://fuchsia.com/context_engine#meta/context_engine.cmx";
constexpr char kContextEngineComponentNamespace[] = "context_engine";

constexpr char kModuleResolverUrl[] =
    "fuchsia-pkg://fuchsia.com/module_resolver#meta/module_resolver.cmx";

constexpr char kSessionEnvironmentLabelPrefix[] = "session-";

constexpr char kMessageQueuePath[] = "/data/MESSAGE_QUEUES/v1/";

constexpr char kSessionShellComponentNamespace[] = "user-shell-namespace";
constexpr char kSessionShellLinkName[] = "user-shell-link";

constexpr char kClipboardAgentUrl[] =
    "fuchsia-pkg://fuchsia.com/clipboard_agent#meta/clipboard_agent.cmx";

constexpr char kLedgerRepositoryDirectory[] = "/data/LEDGER";

// The name in the outgoing debug directory (hub) for developer session control
// services.
constexpr char kSessionCtlDir[] = "sessionctl";

fuchsia::ledger::cloud::firestore::Config GetLedgerFirestoreConfig(
    const std::string& user_profile_id) {
  fuchsia::ledger::cloud::firestore::Config config;
  config.server_id = kFirebaseProjectId;
  config.api_key = kFirebaseApiKey;
  config.user_profile_id = user_profile_id;
  return config;
}

// Creates a function that can be used as termination action passed to AtEnd(),
// which when called invokes the reset() method on the object pointed to by the
// argument. Used to reset() fidl pointers and std::unique_ptr<>s fields.
template <typename X>
fit::function<void(fit::function<void()>)> Reset(
    std::unique_ptr<X>* const field) {
  return [field](fit::function<void()> cont) {
    field->reset();
    cont();
  };
}

template <typename X>
fit::function<void(fit::function<void()>)> Reset(
    fidl::InterfacePtr<X>* const field) {
  return [field](fit::function<void()> cont) {
    field->Unbind();
    cont();
  };
}

// Creates a function that can be used as termination action passed to AtEnd(),
// which when called asynchronously invokes the Teardown() method on the object
// pointed to by the argument. Used to teardown AppClient and AsyncHolder
// members.
template <typename X>
fit::function<void(fit::function<void()>)> Teardown(const zx::duration timeout,
                                                    const char* const message,
                                                    X* const field) {
  return [timeout, message, field](fit::function<void()> cont) {
    field->Teardown(timeout, [message, cont = std::move(cont)] {
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

SessionmgrImpl::SessionmgrImpl(
    component::StartupContext* const startup_context,
    fuchsia::modular::internal::SessionmgrConfig config)
    : startup_context_(startup_context),
      config_(std::move(config)),
      story_provider_impl_("StoryProviderImpl"),
      agent_runner_("AgentRunner"),
      weak_ptr_factory_(this) {
  startup_context_->outgoing()
      .AddPublicService<fuchsia::modular::internal::Sessionmgr>(
          [this](fidl::InterfaceRequest<fuchsia::modular::internal::Sessionmgr>
                     request) {
            bindings_.AddBinding(this, std::move(request));
          });
}

SessionmgrImpl::~SessionmgrImpl() = default;

void SessionmgrImpl::Initialize(
    std::string session_id, fuchsia::modular::auth::AccountPtr account,
    fuchsia::modular::AppConfig session_shell_config,
    fuchsia::modular::AppConfig story_shell_config,
    bool use_session_shell_for_story_shell_factory,
    fidl::InterfaceHandle<fuchsia::auth::TokenManager> ledger_token_manager,
    fidl::InterfaceHandle<fuchsia::auth::TokenManager> agent_token_manager,
    fidl::InterfaceHandle<fuchsia::modular::internal::SessionContext>
        session_context,
    fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
        view_owner_request) {
  FXL_LOG(INFO) << "SessionmgrImpl::Initialize() called.";
  // This is called in the service connection factory callbacks for session
  // shell (see how RunSessionShell() initializes session_shell_services_) to
  // lazily initialize the following services only once they are requested
  // for the first time.
  finish_initialization_ =
      [this, called = false, session_shell_url = session_shell_config.url,
       ledger_token_manager = std::move(ledger_token_manager),
       story_shell_config = std::move(story_shell_config),
       use_session_shell_for_story_shell_factory]() mutable {
        if (called) {
          return;
        }
        FXL_LOG(INFO)
            << "SessionmgrImpl::Initialize() finishing initialization.";
        called = true;

        InitializeLedger(std::move(ledger_token_manager));
        InitializeDeviceMap();
        InitializeMessageQueueManager();
        InitializeMaxwellAndModular(std::move(session_shell_url),
                                    std::move(story_shell_config),
                                    use_session_shell_for_story_shell_factory);
        ConnectSessionShellToStoryProvider();
        AtEnd([this](fit::function<void()> cont) {
          TerminateSessionShell(std::move(cont));
        });
        InitializeClipboard();
        ReportEvent(ModularEvent::BOOTED_TO_SESSIONMGR);
      };

  session_context_ = session_context.Bind();
  AtEnd(Reset(&session_context_));

  InitializeSessionEnvironment(session_id);
  InitializeUser(std::move(account), std::move(agent_token_manager));
  InitializeSessionShell(std::move(session_shell_config),
                         scenic::ToViewToken(zx::eventpair(
                             view_owner_request.TakeChannel().release())));
}

void SessionmgrImpl::ConnectSessionShellToStoryProvider() {
  fuchsia::modular::SessionShellPtr session_shell;
  session_shell_app_->services().ConnectToService(session_shell.NewRequest());
  story_provider_impl_->SetSessionShell(std::move(session_shell));
}

void SessionmgrImpl::InitializeSessionEnvironment(std::string session_id) {
  session_id_ = session_id;

  static const auto* const kEnvServices = new std::vector<std::string>{
      fuchsia::modular::DeviceMap::Name_, fuchsia::modular::Clipboard::Name_};
  session_environment_ = std::make_unique<Environment>(
      startup_context_->environment(),
      std::string(kSessionEnvironmentLabelPrefix) + session_id_, *kEnvServices,
      /* kill_on_oom = */ true);
  AtEnd(Reset(&session_environment_));
}

void SessionmgrImpl::InitializeUser(
    fuchsia::modular::auth::AccountPtr account,
    fidl::InterfaceHandle<fuchsia::auth::TokenManager> agent_token_manager) {
  agent_token_manager_ = agent_token_manager.Bind();
  AtEnd(Reset(&agent_token_manager_));

  account_ = std::move(account);
  AtEnd(Reset(&account_));
}

zx::channel SessionmgrImpl::GetLedgerRepositoryDirectory() {
  if ((config_.use_memfs_for_ledger())) {
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
  fxl::UniqueFD dir(open(kLedgerRepositoryDirectory, O_RDONLY));
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

  ledger_app_ =
      std::make_unique<AppClient<fuchsia::ledger::internal::LedgerController>>(
          session_environment_->GetLauncher(), std::move(ledger_config), "",
          nullptr);
  ledger_app_->SetAppErrorHandler([this] {
    FXL_LOG(ERROR) << "Ledger seems to have crashed unexpectedly." << std::endl
                   << "CALLING Logout() DUE TO UNRECOVERABLE LEDGER ERROR.";
    Shutdown();
  });
  AtEnd(Teardown(kBasicTimeout, "Ledger", ledger_app_.get()));

  fuchsia::ledger::cloud::CloudProviderPtr cloud_provider;
  std::string ledger_user_id;
  if (account_ && (config_.cloud_provider()) !=
                      fuchsia::modular::internal::CloudProvider::NONE) {
    // If not running in Guest mode, configure the cloud provider for Ledger to
    // use for syncing.

    if ((config_.cloud_provider()) ==
        fuchsia::modular::internal::CloudProvider::FROM_ENVIRONMENT) {
      startup_context_->ConnectToEnvironmentService(
          cloud_provider.NewRequest());
    } else if (config_.cloud_provider() ==
               fuchsia::modular::internal::CloudProvider::LET_LEDGER_DECIDE) {
      cloud_provider = LaunchCloudProvider(account_->profile_id,
                                           std::move(ledger_token_manager));
    }

    ledger_user_id = account_->profile_id;
  }

  ledger_repository_factory_.set_error_handler([this](zx_status_t status) {
    FXL_LOG(ERROR) << "LedgerRepositoryFactory.GetRepository() failed: "
                   << LedgerEpitaphToString(status) << std::endl
                   << "CALLING Shutdown() DUE TO UNRECOVERABLE LEDGER ERROR.";
    Shutdown();
  });
  ledger_app_->services().ConnectToService(
      ledger_repository_factory_.NewRequest());
  AtEnd(Reset(&ledger_repository_factory_));

  // The directory "/data" is the data root "/data/LEDGER" that the ledger app
  // client is configured to.
  ledger_repository_factory_->GetRepository(
      GetLedgerRepositoryDirectory(), std::move(cloud_provider), ledger_user_id,
      ledger_repository_.NewRequest());

  // If ledger state is erased from underneath us (happens when the cloud store
  // is cleared), ledger will close the connection to |ledger_repository_|.
  ledger_repository_.set_error_handler([this](zx_status_t status) {
    FXL_LOG(ERROR) << "LedgerRepository disconnected with epitaph: "
                   << LedgerEpitaphToString(status) << std::endl
                   << "CALLING Shutdown() DUE TO UNRECOVERABLE LEDGER ERROR.";
    Shutdown();
  });
  AtEnd(Reset(&ledger_repository_));

  ledger_client_ = std::make_unique<LedgerClient>(
      ledger_repository_.get(), kAppId, [this](zx_status_t status) {
        FXL_LOG(ERROR) << "CALLING Logout() DUE TO UNRECOVERABLE LEDGER ERROR.";
        Shutdown();
      });
  AtEnd(Reset(&ledger_client_));
}

void SessionmgrImpl::InitializeDeviceMap() {
  // fuchsia::modular::DeviceMap service
  const std::string device_id = LoadDeviceID(session_id_);
  device_name_ = LoadDeviceName(session_id_);
  const std::string device_profile = LoadDeviceProfile();

  device_map_impl_ = std::make_unique<DeviceMapImpl>(
      device_name_, device_id, device_profile, ledger_client_.get(),
      fuchsia::ledger::PageId());
  session_environment_->AddService<fuchsia::modular::DeviceMap>(
      [this](fidl::InterfaceRequest<fuchsia::modular::DeviceMap> request) {
        if (terminating_) {
          return;
        }
        // device_map_impl_ may be reset before session_environment_.
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
  session_environment_->AddService<fuchsia::modular::Clipboard>(
      [this](fidl::InterfaceRequest<fuchsia::modular::Clipboard> request) {
        if (terminating_) {
          return;
        }
        services_from_clipboard_agent_->ConnectToService(
            fuchsia::modular::Clipboard::Name_, request.TakeChannel());
      });
}

void SessionmgrImpl::InitializeMessageQueueManager() {
  std::string message_queue_path = kMessageQueuePath;
  message_queue_path.append(session_id_);
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
    fuchsia::modular::AppConfig story_shell_config,
    bool use_session_shell_for_story_shell_factory) {
  // NOTE: There is an awkward service exchange here between
  // AgentRunner, StoryProviderImpl, FocusHandler, VisibleStoriesHandler.
  //
  // AgentRunner needs a UserIntelligenceProvider. Initializing the
  // Maxwell process UserIntelligenceProvider requires a ComponentContext.
  // ComponentContext requires an AgentRunner, which creates a circular
  // dependency.
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
        if (terminating_) {
          return;
        }
        visible_stories_handler_->AddProviderBinding(std::move(request));
      },
      [this](fidl::InterfaceRequest<fuchsia::modular::StoryProvider> request) {
        if (terminating_) {
          return;
        }
        story_provider_impl_->Connect(std::move(request));
      },
      [this](fidl::InterfaceRequest<fuchsia::modular::FocusProvider> request) {
        if (terminating_) {
          return;
        }
        focus_handler_->AddProviderBinding(std::move(request));
      },
      [this](fidl::InterfaceRequest<fuchsia::modular::PuppetMaster> request) {
        if (terminating_) {
          return;
        }
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
      session_environment_->GetLauncher(), message_queue_manager_.get(),
      ledger_repository_.get(), agent_runner_storage_.get(),
      agent_token_manager_.get(), user_intelligence_provider_impl_.get(),
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
            session_environment_->GetLauncher(),
            std::move(context_engine_config), "" /* data_origin */,
            std::move(service_list));
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
      std::move(maxwell_app_component_context), (config_.session_agents()),
      (config_.startup_agents()));

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

    // For now, we want data_origin to be "", which uses our (parent process's)
    // /data. This is appropriate for the module_resolver. We can in the future
    // isolate the data it reads to a subdir of /data and map that in here.
    module_resolver_app_ =
        std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
            session_environment_->GetLauncher(),
            std::move(module_resolver_config), "" /* data_origin */,
            std::move(service_list));
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

  // The StoryShellFactory to use when creating story shells, or nullptr if no
  // such factory exists.
  fidl::InterfacePtr<fuchsia::modular::StoryShellFactory>
      story_shell_factory_ptr;

  if (use_session_shell_for_story_shell_factory) {
    session_shell_app_->services().ConnectToService(
        story_shell_factory_ptr.NewRequest());
  }

  fidl::InterfacePtr<fuchsia::modular::FocusProvider>
      focus_provider_story_provider;
  auto focus_provider_request_story_provider =
      focus_provider_story_provider.NewRequest();

  presentation_provider_impl_ =
      std::make_unique<PresentationProviderImpl>(this);
  AtEnd(Reset(&presentation_provider_impl_));

  // We create |story_provider_impl_| after |agent_runner_| so
  // story_provider_impl_ is terminated before agent_runner_, which will cause
  // all modules to be terminated before agents are terminated. Agents must
  // outlive the stories which contain modules that are connected to those
  // agents.
  session_storage_ = std::make_unique<SessionStorage>(
      ledger_client_.get(), fuchsia::ledger::PageId());

  module_facet_reader_.reset(new ModuleFacetReaderImpl(
      startup_context_->ConnectToEnvironmentService<fuchsia::sys::Loader>()));

  story_provider_impl_.reset(new StoryProviderImpl(
      session_environment_.get(), device_map_impl_->current_device_id(),
      session_storage_.get(), std::move(story_shell_config),
      std::move(story_shell_factory_ptr), component_context_info,
      std::move(focus_provider_story_provider),
      user_intelligence_provider_impl_.get(), module_resolver_service_.get(),
      entity_provider_runner_.get(), module_facet_reader_.get(),
      presentation_provider_impl_.get(),
      startup_context_
          ->ConnectToEnvironmentService<fuchsia::ui::viewsv1::ViewSnapshot>(),
      (config_.enable_story_shell_preload())));
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
          std::string story_id, std::vector<std::string> mod_name) {
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
    fuchsia::modular::AppConfig session_shell_config,
    fuchsia::ui::views::ViewToken view_token) {
  // We setup our own view and make the fuchsia::modular::SessionShell a child
  // of it.
  auto scenic =
      startup_context_
          ->ConnectToEnvironmentService<fuchsia::ui::scenic::Scenic>();
  scenic::ViewContext view_context = {
      .session_and_listener_request =
          scenic::CreateScenicSessionPtrAndListenerRequest(scenic.get()),
      .view_token2 = std::move(view_token),
      .startup_context = startup_context_,
  };
  session_shell_view_host_ =
      std::make_unique<ViewHost>(std::move(view_context));
  RunSessionShell(std::move(session_shell_config));
}

void SessionmgrImpl::RunSessionShell(
    fuchsia::modular::AppConfig session_shell_config) {
  // |session_shell_services_| is a ServiceProvider (aka a Directory) that
  // augments the session shell's namespace.
  //
  // |service_list| enumerates which services are made available to the session
  // shell.
  auto service_list = fuchsia::sys::ServiceList::New();

  service_list->names.push_back(fuchsia::modular::SessionShellContext::Name_);
  session_shell_services_.AddService<fuchsia::modular::SessionShellContext>(
      [this](auto request) {
        if (terminating_) {
          return;
        }
        finish_initialization_();
        session_shell_context_bindings_.AddBinding(this, std::move(request));
      });

  service_list->names.push_back(fuchsia::modular::ComponentContext::Name_);
  session_shell_services_.AddService<fuchsia::modular::ComponentContext>(
      [this](auto request) {
        if (terminating_) {
          return;
        }
        finish_initialization_();
        session_shell_component_context_impl_->Connect(std::move(request));
      });

  service_list->names.push_back(fuchsia::modular::PuppetMaster::Name_);
  session_shell_services_.AddService<fuchsia::modular::PuppetMaster>(
      [this](auto request) {
        if (terminating_) {
          return;
        }
        finish_initialization_();
        puppet_master_impl_->Connect(std::move(request));
      });

  service_list->names.push_back(fuchsia::modular::IntelligenceServices::Name_);
  session_shell_services_.AddService<fuchsia::modular::IntelligenceServices>(
      [this](auto request) {
        if (terminating_) {
          return;
        }
        finish_initialization_();
        fuchsia::modular::ComponentScope component_scope;
        component_scope.set_global_scope(fuchsia::modular::GlobalScope());
        user_intelligence_provider_impl_->GetComponentIntelligenceServices(
            std::move(component_scope), std::move(request));
      });

  // The services in |session_shell_services_| are provided through the
  // connection held in |session_shell_service_provider| connected to
  // |session_shell_services_|.
  {
    fuchsia::sys::ServiceProviderPtr session_shell_service_provider;
    session_shell_services_.AddBinding(
        session_shell_service_provider.NewRequest());
    service_list->provider = std::move(session_shell_service_provider);
  }

  session_shell_app_ = std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
      session_environment_->GetLauncher(), std::move(session_shell_config),
      /* data_origin = */ "", std::move(service_list));

  session_shell_app_->SetAppErrorHandler([this] {
    FXL_LOG(ERROR) << "Session Shell seems to have crashed unexpectedly."
                   << " Shutting down.";
    Shutdown();
  });

  auto [view_token, view_holder_token] = scenic::NewViewTokenPair();
  fuchsia::ui::app::ViewProviderPtr view_provider;
  session_shell_app_->services().ConnectToService(view_provider.NewRequest());
  view_provider->CreateView(std::move(view_token.value), nullptr, nullptr);
  session_shell_view_host_->ConnectView(std::move(view_holder_token));
}

void SessionmgrImpl::TerminateSessionShell(fit::function<void()> callback) {
  session_shell_app_->Teardown(kBasicTimeout,
                               [weak_ptr = weak_ptr_factory_.GetWeakPtr(),
                                callback = std::move(callback)] {
                                 callback();
                                 if (weak_ptr) {
                                   weak_ptr->session_shell_app_.reset();
                                 }
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
        sessionmgr_impl_->ConnectSessionShellToStoryProvider();
      });
    });
  }

  SessionmgrImpl* const sessionmgr_impl_;
  fuchsia::modular::AppConfig session_shell_config_;
};

void SessionmgrImpl::SwapSessionShell(
    fuchsia::modular::AppConfig session_shell_config,
    SwapSessionShellCallback callback) {
  operation_queue_.Add(std::make_unique<SwapSessionShellOperation>(
      this, std::move(session_shell_config), std::move(callback)));
}

void SessionmgrImpl::Terminate(fit::function<void()> done) {
  FXL_LOG(INFO) << "Sessionmgr::Terminate()";
  terminating_ = true;
  at_end_done_ = std::move(done);

  TerminateRecurse(at_end_.size() - 1);
}

void SessionmgrImpl::GetAccount(
    fit::function<void(::std::unique_ptr<::fuchsia::modular::auth::Account>)>
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
    fit::function<void(::std::string)> callback) {
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
  session_context_->GetPresentation(std::move(request));
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

void SessionmgrImpl::Logout() { session_context_->Logout(); }

void SessionmgrImpl::Shutdown() { session_context_->Shutdown(); }

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

void SessionmgrImpl::ConnectToStoryEntityProvider(
    const std::string& story_id,
    fidl::InterfaceRequest<fuchsia::modular::EntityProvider>
        entity_provider_request) {
  story_provider_impl_->ConnectToStoryEntityProvider(
      story_id, std::move(entity_provider_request));
}

fuchsia::ledger::cloud::CloudProviderPtr SessionmgrImpl::LaunchCloudProvider(
    const std::string& user_profile_id,
    fidl::InterfaceHandle<fuchsia::auth::TokenManager> ledger_token_manager) {
  FXL_CHECK(ledger_token_manager);

  fuchsia::modular::AppConfig cloud_provider_app_config;
  cloud_provider_app_config.url = kCloudProviderFirestoreAppUrl;
  cloud_provider_app_ =
      std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
          session_environment_->GetLauncher(),
          std::move(cloud_provider_app_config));
  cloud_provider_app_->services().ConnectToService(
      cloud_provider_factory_.NewRequest());
  // TODO(mesch): Teardown cloud_provider_app_ ?

  fuchsia::ledger::cloud::CloudProviderPtr cloud_provider;
  auto cloud_provider_config = GetLedgerFirestoreConfig(user_profile_id);

  cloud_provider_factory_->GetCloudProvider(
      std::move(cloud_provider_config), std::move(ledger_token_manager),
      cloud_provider.NewRequest(), [](fuchsia::ledger::cloud::Status status) {
        if (status != fuchsia::ledger::cloud::Status::OK) {
          FXL_LOG(ERROR) << "Failed to create a cloud provider: "
                         << fidl::ToUnderlying(status);
        }
      });
  return cloud_provider;
}

void SessionmgrImpl::AtEnd(fit::function<void(fit::function<void()>)> action) {
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
