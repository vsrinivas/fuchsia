// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/sessionmgr_impl.h"

#include <fcntl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/eventpair.h>
#include <zircon/status.h>

#include "peridot/lib/ledger_client/constants.h"
#include "peridot/lib/ledger_client/ledger_client.h"
#include "peridot/lib/ledger_client/page_id.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fsl/io/fd.h"
#include "src/lib/fsl/types/type_converters.h"
#include "src/lib/fxl/logging.h"
#include "src/modular/bin/basemgr/cobalt/cobalt.h"
#include "src/modular/bin/sessionmgr/agent_runner/map_agent_service_index.h"
#include "src/modular/bin/sessionmgr/component_context_impl.h"
#include "src/modular/bin/sessionmgr/focus.h"
#include "src/modular/bin/sessionmgr/presentation_provider.h"
#include "src/modular/bin/sessionmgr/puppet_master/make_production_impl.h"
#include "src/modular/bin/sessionmgr/puppet_master/puppet_master_impl.h"
#include "src/modular/bin/sessionmgr/puppet_master/story_command_executor.h"
#include "src/modular/bin/sessionmgr/session_ctl.h"
#include "src/modular/bin/sessionmgr/storage/constants_and_utils.h"
#include "src/modular/bin/sessionmgr/storage/session_storage.h"
#include "src/modular/bin/sessionmgr/story_runner/story_controller_impl.h"
#include "src/modular/bin/sessionmgr/story_runner/story_provider_impl.h"
#include "src/modular/lib/common/teardown.h"
#include "src/modular/lib/device_info/device_info.h"
#include "src/modular/lib/fidl/array_to_string.h"
#include "src/modular/lib/fidl/json_xdr.h"
#include "src/modular/lib/module_manifest/module_facet_reader_impl.h"

namespace modular {

using cobalt_registry::ModularLifetimeEventsMetricDimensionEventType;

namespace {

constexpr char kAppId[] = "modular_sessionmgr";

constexpr char kDiscovermgrUrl[] = "fuchsia-pkg://fuchsia.com/discovermgr#meta/discovermgr.cmx";

constexpr char kSessionEnvironmentLabelPrefix[] = "session-";

constexpr char kSessionShellComponentNamespace[] = "user-shell-namespace";

constexpr char kLedgerRepositoryDirectory[] = "/data/LEDGER";

// The name in the outgoing debug directory (hub) for developer session control
// services.
constexpr char kSessionCtlDir[] = "sessionctl";

// Creates a function that can be used as termination action passed to AtEnd(),
// which when called invokes the reset() method on the object pointed to by the
// argument. Used to reset() fidl pointers and std::unique_ptr<>s fields.
template <typename X>
fit::function<void(fit::function<void()>)> Reset(std::unique_ptr<X>* const field) {
  return [field](fit::function<void()> cont) {
    field->reset();
    cont();
  };
}

template <typename X>
fit::function<void(fit::function<void()>)> Reset(fidl::InterfacePtr<X>* const field) {
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
                                                    const char* const message, X* const field) {
  return [timeout, message, field](fit::function<void()> cont) {
    field->Teardown(timeout, [message, cont = std::move(cont)] {
      if (message) {
        FXL_DLOG(INFO) << "- " << message << " down.";
      }
      cont();
    });
  };
}

fit::function<void(fit::function<void()>)> ResetLedgerRepository(
    fuchsia::ledger::internal::LedgerRepositoryPtr* const ledger_repository) {
  return [ledger_repository](fit::function<void()> cont) {
    ledger_repository->set_error_handler([cont = std::move(cont)](zx_status_t status) {
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "LedgerRepository disconnected with epitaph: "
                       << zx_status_get_string(status) << std::endl;
      }
      cont();
    });
    (*ledger_repository)->Close();
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
                       fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) override {
    if (impl_->session_shell_app_) {
      fuchsia::modular::SessionShellPresentationProviderPtr provider;
      impl_->session_shell_app_->services().ConnectToService(provider.NewRequest());
      provider->GetPresentation(story_id.value_or(""), std::move(request));
    }
  }

  void WatchVisualState(
      fidl::StringPtr story_id,
      fidl::InterfaceHandle<fuchsia::modular::StoryVisualStateWatcher> watcher) override {
    if (impl_->session_shell_app_) {
      fuchsia::modular::SessionShellPresentationProviderPtr provider;
      impl_->session_shell_app_->services().ConnectToService(provider.NewRequest());
      provider->WatchVisualState(story_id.value_or(""), std::move(watcher));
    }
  }

  SessionmgrImpl* const impl_;
};

SessionmgrImpl::SessionmgrImpl(sys::ComponentContext* const component_context,
                               fuchsia::modular::session::SessionmgrConfig config,
                               inspect::Node node_object)
    : sessionmgr_context_(component_context),
      config_(std::move(config)),
      inspect_root_node_(std::move(node_object)),
      story_provider_impl_("StoryProviderImpl"),
      agent_runner_("AgentRunner"),
      weak_ptr_factory_(this) {
  sessionmgr_context_->outgoing()->AddPublicService<fuchsia::modular::internal::Sessionmgr>(
      [this](fidl::InterfaceRequest<fuchsia::modular::internal::Sessionmgr> request) {
        bindings_.AddBinding(this, std::move(request));
      });
}

SessionmgrImpl::~SessionmgrImpl() = default;

// Initialize is called for each new session, denoted by a unique session_id. In other words, it
// initializes a session, not a SessionmgrImpl (despite the class-scoped name). (Ironically, the
// |finitish_initialization_| lambda does initialize some Sessionmgr-scoped resources only once,
// upon demand.)
void SessionmgrImpl::Initialize(
    std::string session_id, fuchsia::modular::auth::AccountPtr account,
    fuchsia::modular::AppConfig session_shell_config,
    fuchsia::modular::AppConfig story_shell_config, bool use_session_shell_for_story_shell_factory,
    fidl::InterfaceHandle<fuchsia::auth::TokenManager> agent_token_manager,
    fidl::InterfaceHandle<fuchsia::modular::internal::SessionContext> session_context,
    fuchsia::ui::views::ViewToken view_token) {
  FXL_LOG(INFO) << "SessionmgrImpl::Initialize() called.";

  // This is called in the service connection factory callbacks for session
  // shell (see how RunSessionShell() initializes session_shell_services_) to
  // lazily initialize the following services only once they are requested
  // for the first time.
  finish_initialization_ = [this, called = false, session_shell_url = session_shell_config.url,
                            story_shell_config = std::move(story_shell_config),
                            use_session_shell_for_story_shell_factory]() mutable {
    if (called) {
      return;
    }
    FXL_LOG(INFO) << "SessionmgrImpl::Initialize() finishing initialization.";
    called = true;

    InitializeLedger();
    InitializeIntlPropertyProvider();
    InitializeDiscovermgr();
    InitializeModular(std::move(session_shell_url), std::move(story_shell_config),
                      use_session_shell_for_story_shell_factory);
    ConnectSessionShellToStoryProvider();
    AtEnd([this](fit::function<void()> cont) { TerminateSessionShell(std::move(cont)); });
    ReportEvent(ModularLifetimeEventsMetricDimensionEventType::BootedToSessionMgr);
  };

  session_context_ = session_context.Bind();
  AtEnd(Reset(&session_context_));

  InitializeSessionEnvironment(session_id);
  InitializeUser(std::move(account), std::move(agent_token_manager));
  InitializeSessionShell(std::move(session_shell_config), std::move(view_token));
}

void SessionmgrImpl::ConnectSessionShellToStoryProvider() {
  fuchsia::modular::SessionShellPtr session_shell;
  session_shell_app_->services().ConnectToService(session_shell.NewRequest());
  story_provider_impl_->SetSessionShell(std::move(session_shell));
}

// Create an environment in which to launch story shells and mods. Note that agents cannot be
// launched from this environment because the environment hosts its data directories in a
// session-specific subdirectory of data, and certain agents in existing test devices expect the
// data at a hardcoded, top-level /data directory.
//
// True separation among multiple sessions is currently NOT supported for many reasons, so as
// a temporary workaround, agents are started in the /sys realm via a different launcher.
//
// Future implementations will use the new SessionFramework, which will provide support for
// multiple sessions.
void SessionmgrImpl::InitializeSessionEnvironment(std::string session_id) {
  session_id_ = session_id;

  // Use this launcher to launch components in sessionmgr's component context's environment
  // (such as the Ledger).
  sessionmgr_context_launcher_ = sessionmgr_context_->svc()->Connect<fuchsia::sys::Launcher>();

  // Create the session's environment (in which we run stories, modules, agents, and so on) as a
  // child of sessionmgr's environment. Add session-provided additional services, |kEnvServices|.
  static const auto* const kEnvServices =
      new std::vector<std::string>{fuchsia::intl::PropertyProvider::Name_};
  session_environment_ = std::make_unique<Environment>(
      /* parent_env = */ sessionmgr_context_->svc()->Connect<fuchsia::sys::Environment>(),
      std::string(kSessionEnvironmentLabelPrefix) + session_id_, *kEnvServices,
      /* kill_on_oom = */ true);

  // Get the default launcher from the new |session_environment_| to wrap in an
  // |ArgvInjectingLauncher|
  fuchsia::sys::LauncherPtr session_environment_launcher;
  session_environment_->environment()->GetLauncher(session_environment_launcher.NewRequest());

  // Wrap the launcher and override it with the new |ArgvInjectingLauncher|
  ArgvInjectingLauncher::ArgvMap argv_map;
  for (auto& component : config_.component_args()) {
    argv_map.insert(std::make_pair(component.url(), component.args()));
  }
  session_environment_->OverrideLauncher(
      std::make_unique<ArgvInjectingLauncher>(std::move(session_environment_launcher), argv_map));

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
    FXL_LOG(ERROR) << "Unable to create directory at " << kLedgerRepositoryDirectory;
    return zx::channel();
  }
  fbl::unique_fd dir(open(kLedgerRepositoryDirectory, O_RDONLY));
  if (!dir.is_valid()) {
    FXL_LOG(ERROR) << "Unable to open directory at " << kLedgerRepositoryDirectory
                   << ". errno: " << errno;
    return zx::channel();
  }

  return fsl::CloneChannelFromFileDescriptor(dir.get());
}

void SessionmgrImpl::InitializeLedger() {
  fuchsia::modular::AppConfig ledger_config;
  ledger_config.url = kLedgerAppUrl;

  ledger_app_ = std::make_unique<AppClient<fuchsia::ledger::internal::LedgerController>>(
      sessionmgr_context_launcher_.get(), std::move(ledger_config), "", nullptr);
  ledger_app_->SetAppErrorHandler([this] {
    FXL_LOG(ERROR) << "Ledger seems to have crashed unexpectedly." << std::endl
                   << "CALLING Logout() DUE TO UNRECOVERABLE LEDGER ERROR.";
    Shutdown();
  });
  AtEnd(Teardown(kBasicTimeout, "Ledger", ledger_app_.get()));

  auto repository_request = ledger_repository_.NewRequest();
  ledger_client_ =
      std::make_unique<LedgerClient>(ledger_repository_.get(), kAppId, [this](zx_status_t status) {
        FXL_LOG(ERROR) << "CALLING Logout() DUE TO UNRECOVERABLE LEDGER ERROR.";
        Shutdown();
      });

  ledger_repository_factory_.set_error_handler([this](zx_status_t status) {
    FXL_LOG(ERROR) << "LedgerRepositoryFactory.GetRepository() failed: "
                   << zx_status_get_string(status) << std::endl
                   << "CALLING Shutdown() DUE TO UNRECOVERABLE LEDGER ERROR.";
    Shutdown();
  });
  ledger_app_->services().ConnectToService(ledger_repository_factory_.NewRequest());
  AtEnd(Reset(&ledger_repository_factory_));

  // The directory "/data" is the data root "/data/LEDGER" that the ledger app
  // client is configured to.
  ledger_repository_factory_->GetRepository(GetLedgerRepositoryDirectory(), nullptr, "",
                                            std::move(repository_request));

  // If ledger state is erased from underneath us (happens when the cloud store
  // is cleared), ledger will close the connection to |ledger_repository_|.
  ledger_repository_.set_error_handler([this](zx_status_t status) {
    FXL_LOG(ERROR) << "LedgerRepository disconnected with epitaph: " << zx_status_get_string(status)
                   << std::endl
                   << "CALLING Shutdown() DUE TO UNRECOVERABLE LEDGER ERROR.";
    Shutdown();
  });
  AtEnd(ResetLedgerRepository(&ledger_repository_));

  AtEnd(Reset(&ledger_client_));
}

void SessionmgrImpl::InitializeIntlPropertyProvider() {
  session_environment_->AddService<fuchsia::intl::PropertyProvider>(
      [this](fidl::InterfaceRequest<fuchsia::intl::PropertyProvider> request) {
        if (terminating_) {
          return;
        }
        sessionmgr_context_->svc()->Connect<fuchsia::intl::PropertyProvider>(std::move(request));
      });
}

void SessionmgrImpl::InitializeModular(const fidl::StringPtr& session_shell_url,
                                       fuchsia::modular::AppConfig story_shell_config,
                                       bool use_session_shell_for_story_shell_factory) {
  startup_agent_launcher_.reset(new StartupAgentLauncher(
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
      },
      [this](fidl::InterfaceRequest<fuchsia::intl::PropertyProvider> request) {
        if (terminating_) {
          return;
        }
        sessionmgr_context_->svc()->Connect<fuchsia::intl::PropertyProvider>(std::move(request));
      },
      [this]() { return terminating_; }));
  AtEnd(Reset(&startup_agent_launcher_));

  entity_provider_runner_ =
      std::make_unique<EntityProviderRunner>(static_cast<EntityProviderLauncher*>(this));
  AtEnd(Reset(&entity_provider_runner_));

  std::map<std::string, std::string> service_to_agent_map;
  for (auto& entry : config_.agent_service_index()) {
    service_to_agent_map.emplace(entry.service_name(), entry.agent_url());
  }
  auto agent_service_index =
      std::make_unique<MapAgentServiceIndex>(std::move(service_to_agent_map));

  // Initialize the AgentRunner.
  //
  // The AgentRunner must use its own |ArgvInjectingLauncher|, different from the
  // |ArgvInjectingLauncher| launcher used for mods: The AgentRunner's launcher must come from the
  // sys realm (the realm that sessionmgr is running in) due to devices in the field which rely on
  // agents /data path mappings being consistent. There is no current solution for the migration of
  // /data when a component topology changes. This will be resolved in Session Framework, which
  // will soon deprecated and replace this Modular solution.
  //
  // Create a new launcher that uses sessionmgr's realm launcher.
  ArgvInjectingLauncher::ArgvMap argv_map;
  for (auto& component : config_.component_args()) {
    argv_map.insert(std::make_pair(component.url(), component.args()));
  }
  agent_runner_launcher_ = std::make_unique<ArgvInjectingLauncher>(
      sessionmgr_context_->svc()->Connect<fuchsia::sys::Launcher>(), argv_map);
  agent_runner_.reset(new AgentRunner(agent_runner_launcher_.get(), agent_token_manager_.get(),
                                      startup_agent_launcher_.get(), entity_provider_runner_.get(),
                                      &inspect_root_node_, std::move(agent_service_index),
                                      sessionmgr_context_));
  AtEnd(Teardown(kAgentRunnerTimeout, "AgentRunner", &agent_runner_));

  ComponentContextInfo component_context_info{agent_runner_.get(), entity_provider_runner_.get()};

  startup_agent_launcher_->StartAgents(agent_runner_.get(), config_.session_agents(),
                                       config_.startup_agents());

  local_module_resolver_ = std::make_unique<LocalModuleResolver>();
  AtEnd(Reset(&local_module_resolver_));

  session_shell_component_context_impl_ = std::make_unique<ComponentContextImpl>(
      component_context_info, kSessionShellComponentNamespace, session_shell_url.value_or(""),
      session_shell_url.value_or(""));

  AtEnd(Reset(&session_shell_component_context_impl_));

  // The StoryShellFactory to use when creating story shells, or nullptr if no
  // such factory exists.
  fidl::InterfacePtr<fuchsia::modular::StoryShellFactory> story_shell_factory_ptr;

  if (use_session_shell_for_story_shell_factory) {
    session_shell_app_->services().ConnectToService(story_shell_factory_ptr.NewRequest());
  }

  fidl::InterfacePtr<fuchsia::modular::FocusProvider> focus_provider_story_provider;
  auto focus_provider_request_story_provider = focus_provider_story_provider.NewRequest();

  presentation_provider_impl_ = std::make_unique<PresentationProviderImpl>(this);
  AtEnd(Reset(&presentation_provider_impl_));

  // We create |story_provider_impl_| after |agent_runner_| so
  // story_provider_impl_ is terminated before agent_runner_, which will cause
  // all modules to be terminated before agents are terminated. Agents must
  // outlive the stories which contain modules that are connected to those
  // agents.

  session_storage_ =
      std::make_unique<SessionStorage>(ledger_client_.get(), fuchsia::ledger::PageId());

  module_facet_reader_.reset(
      new ModuleFacetReaderImpl(sessionmgr_context_->svc()->Connect<fuchsia::sys::Loader>()));

  story_provider_impl_.reset(new StoryProviderImpl(
      session_environment_.get(), LoadDeviceID(session_id_), session_storage_.get(),
      std::move(story_shell_config), std::move(story_shell_factory_ptr), component_context_info,
      std::move(focus_provider_story_provider), startup_agent_launcher_.get(),
      discover_registry_service_.get(),
      static_cast<fuchsia::modular::ModuleResolver*>(local_module_resolver_.get()),
      entity_provider_runner_.get(), module_facet_reader_.get(), presentation_provider_impl_.get(),
      (config_.enable_story_shell_preload()), &inspect_root_node_));

  AtEnd(Teardown(kStoryProviderTimeout, "StoryProvider", &story_provider_impl_));

  fuchsia::modular::FocusProviderPtr focus_provider_puppet_master;
  auto focus_provider_request_puppet_master = focus_provider_puppet_master.NewRequest();

  // Initialize the PuppetMaster.
  //
  // There's no clean runtime interface we can inject to
  // puppet master. Hence, for now we inject this function to be able to focus
  // mods. Capturing a pointer to |story_provider_impl_| is safe because PuppetMaster
  // is destroyed before StoryProviderImpl.
  auto module_focuser = [story_provider_impl = story_provider_impl_.get()](
                            std::string story_id, std::vector<std::string> mod_name) {
    auto story_controller_ptr = story_provider_impl->GetStoryControllerImpl(story_id);
    if (story_controller_ptr == nullptr) {
      return;
    }
    story_controller_ptr->FocusModule(std::move(mod_name));
  };
  AtEnd(Reset(&session_storage_));
  story_command_executor_ = MakeProductionStoryCommandExecutor(
      session_storage_.get(), std::move(focus_provider_puppet_master),
      static_cast<fuchsia::modular::ModuleResolver*>(local_module_resolver_.get()),
      entity_provider_runner_.get(), std::move(module_focuser));
  puppet_master_impl_ =
      std::make_unique<PuppetMasterImpl>(session_storage_.get(), story_command_executor_.get());

  session_ctl_ = std::make_unique<SessionCtl>(sessionmgr_context_->outgoing()->debug_dir(),
                                              kSessionCtlDir, puppet_master_impl_.get());

  AtEnd(Reset(&story_command_executor_));
  AtEnd(Reset(&puppet_master_impl_));
  AtEnd(Reset(&session_ctl_));

  focus_handler_ = std::make_unique<FocusHandler>(LoadDeviceID(session_id_), ledger_client_.get(),
                                                  fuchsia::ledger::PageId());
  focus_handler_->AddProviderBinding(std::move(focus_provider_request_story_provider));
  focus_handler_->AddProviderBinding(std::move(focus_provider_request_puppet_master));
  AtEnd(Reset(&focus_handler_));
}

// TODO(MI4-2416): pass additional configuration.
void SessionmgrImpl::InitializeDiscovermgr() {
  auto service_list = fuchsia::sys::ServiceList::New();
  service_list->names.push_back(fuchsia::modular::PuppetMaster::Name_);
  service_list->names.push_back(fuchsia::modular::EntityResolver::Name_);
  service_list->names.push_back(fuchsia::ledger::Ledger::Name_);
  discovermgr_ns_services_.AddService<fuchsia::modular::PuppetMaster>([this](auto request) {
    if (terminating_) {
      return;
    }
    puppet_master_impl_->Connect(std::move(request));
  });
  discovermgr_ns_services_.AddService<fuchsia::modular::EntityResolver>([this](auto request) {
    if (terminating_) {
      return;
    }
    entity_provider_runner_->ConnectEntityResolver(std::move(request));
  });
  discovermgr_ns_services_.AddService<fuchsia::ledger::Ledger>([this](auto request) {
    if (terminating_) {
      return;
    }
    ledger_repository_->GetLedger(to_array(kDiscovermgrUrl), std::move(request));
  });
  discovermgr_ns_services_.AddBinding(service_list->provider.NewRequest());

  fuchsia::modular::AppConfig discovermgr_config;
  discovermgr_config.url = kDiscovermgrUrl;

  discovermgr_app_ = std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
      sessionmgr_context_launcher_.get(), std::move(discovermgr_config), "" /* data_origin */,
      std::move(service_list));
  discovermgr_app_->services().ConnectToService(discover_registry_service_.NewRequest());
  AtEnd(Reset(&discover_registry_service_));
  AtEnd(Reset(&discovermgr_app_));
  AtEnd(Teardown(kBasicTimeout, "Discovermgr", discovermgr_app_.get()));
}

void SessionmgrImpl::InitializeSessionShell(fuchsia::modular::AppConfig session_shell_config,
                                            fuchsia::ui::views::ViewToken view_token) {
  // We setup our own view and make the fuchsia::modular::SessionShell a child
  // of it.
  auto scenic = sessionmgr_context_->svc()->Connect<fuchsia::ui::scenic::Scenic>();
  scenic::ViewContext view_context = {
      .session_and_listener_request =
          scenic::CreateScenicSessionPtrAndListenerRequest(scenic.get()),
      .view_token = std::move(view_token),
      .component_context = sessionmgr_context_,
  };
  session_shell_view_host_ = std::make_unique<ViewHost>(std::move(view_context));
  RunSessionShell(std::move(session_shell_config));
}

void SessionmgrImpl::RunSessionShell(fuchsia::modular::AppConfig session_shell_config) {
  // |session_shell_services_| is a ServiceProvider (aka a Directory) that
  // augments the session shell's namespace.
  //
  // |service_list| enumerates which services are made available to the session
  // shell.
  auto service_list = fuchsia::sys::ServiceList::New();

  service_list->names.push_back(fuchsia::modular::SessionShellContext::Name_);
  session_shell_services_.AddService<fuchsia::modular::SessionShellContext>([this](auto request) {
    if (terminating_) {
      return;
    }
    finish_initialization_();
    session_shell_context_bindings_.AddBinding(this, std::move(request));
  });

  service_list->names.push_back(fuchsia::modular::ComponentContext::Name_);
  session_shell_services_.AddService<fuchsia::modular::ComponentContext>([this](auto request) {
    if (terminating_) {
      return;
    }
    finish_initialization_();
    session_shell_component_context_impl_->Connect(std::move(request));
  });

  service_list->names.push_back(fuchsia::modular::PuppetMaster::Name_);
  session_shell_services_.AddService<fuchsia::modular::PuppetMaster>([this](auto request) {
    if (terminating_) {
      return;
    }
    finish_initialization_();
    puppet_master_impl_->Connect(std::move(request));
  });

  service_list->names.push_back(fuchsia::app::discover::Suggestions::Name_);
  session_shell_services_.AddService<fuchsia::app::discover::Suggestions>([this](auto request) {
    if (terminating_) {
      return;
    }
    finish_initialization_();
    discovermgr_app_->services().ConnectToService(std::move(request));
  });

  service_list->names.push_back(fuchsia::app::discover::SessionDiscoverContext::Name_);
  session_shell_services_.AddService<fuchsia::app::discover::SessionDiscoverContext>(
      [this](auto request) {
        if (terminating_) {
          return;
        }
        finish_initialization_();
        discovermgr_app_->services().ConnectToService(std::move(request));
      });

  // The services in |session_shell_services_| are provided through the
  // connection held in |session_shell_service_provider| connected to
  // |session_shell_services_|.
  {
    fuchsia::sys::ServiceProviderPtr session_shell_service_provider;
    session_shell_services_.AddBinding(session_shell_service_provider.NewRequest());
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

  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  fuchsia::ui::app::ViewProviderPtr view_provider;
  session_shell_app_->services().ConnectToService(view_provider.NewRequest());
  view_provider->CreateView(std::move(view_token.value), nullptr, nullptr);
  session_shell_view_host_->ConnectView(std::move(view_holder_token));
}

void SessionmgrImpl::TerminateSessionShell(fit::function<void()> callback) {
  session_shell_app_->Teardown(
      kBasicTimeout, [weak_ptr = weak_ptr_factory_.GetWeakPtr(), callback = std::move(callback)] {
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
      : Operation("SessionmgrImpl::SwapSessionShellOperation", std::move(result_call)),
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

void SessionmgrImpl::SwapSessionShell(fuchsia::modular::AppConfig session_shell_config,
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
    fit::function<void(::std::unique_ptr<::fuchsia::modular::auth::Account>)> callback) {
  callback(fidl::Clone(account_));
}

void SessionmgrImpl::GetComponentContext(
    fidl::InterfaceRequest<fuchsia::modular::ComponentContext> request) {
  session_shell_component_context_impl_->Connect(std::move(request));
}

void SessionmgrImpl::GetFocusController(
    fidl::InterfaceRequest<fuchsia::modular::FocusController> request) {
  focus_handler_->AddControllerBinding(std::move(request));
}

void SessionmgrImpl::GetFocusProvider(
    fidl::InterfaceRequest<fuchsia::modular::FocusProvider> request) {
  focus_handler_->AddProviderBinding(std::move(request));
}

void SessionmgrImpl::GetPresentation(
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) {
  session_context_->GetPresentation(std::move(request));
}

void SessionmgrImpl::GetStoryProvider(
    fidl::InterfaceRequest<fuchsia::modular::StoryProvider> request) {
  story_provider_impl_->Connect(std::move(request));
}

void SessionmgrImpl::Logout() { session_context_->Logout(); }

void SessionmgrImpl::Restart() { session_context_->Restart(); }

void SessionmgrImpl::Shutdown() { session_context_->Shutdown(); }

// |EntityProviderLauncher|
void SessionmgrImpl::ConnectToEntityProvider(
    const std::string& agent_url,
    fidl::InterfaceRequest<fuchsia::modular::EntityProvider> entity_provider_request,
    fidl::InterfaceRequest<fuchsia::modular::AgentController> agent_controller_request) {
  FXL_DCHECK(agent_runner_.get());
  agent_runner_->ConnectToEntityProvider(agent_url, std::move(entity_provider_request),
                                         std::move(agent_controller_request));
}

void SessionmgrImpl::ConnectToStoryEntityProvider(
    const std::string& story_id,
    fidl::InterfaceRequest<fuchsia::modular::EntityProvider> entity_provider_request) {
  story_provider_impl_->ConnectToStoryEntityProvider(story_id, std::move(entity_provider_request));
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
