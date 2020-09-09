// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/sessionmgr_impl.h"

#include <fcntl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/eventpair.h>
#include <zircon/status.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fsl/io/fd.h"
#include "src/lib/fsl/types/type_converters.h"
#include "src/modular/bin/basemgr/cobalt/cobalt.h"
#include "src/modular/bin/sessionmgr/puppet_master/make_production_impl.h"
#include "src/modular/bin/sessionmgr/story_runner/story_controller_impl.h"
#include "src/modular/lib/common/teardown.h"
#include "src/modular/lib/fidl/array_to_string.h"
#include "src/modular/lib/fidl/json_xdr.h"

namespace modular {

using cobalt_registry::ModularLifetimeEventsMetricDimensionEventType;

namespace {

constexpr char kSessionEnvironmentLabelPrefix[] = "session-";

// The name in the outgoing debug directory (hub) for developer session control
// services.
constexpr char kSessionCtlDir[] = "sessionctl";

// Creates a function that can be used as termination action passed to OnTerminate(),
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

// Creates a function that can be used as termination action passed to OnTerminate(),
// which when called asynchronously invokes the Teardown() method on the object
// pointed to by the argument. Used to teardown AppClient and AsyncHolder
// members.
template <typename X>
fit::function<void(fit::function<void()>)> Teardown(const zx::duration timeout,
                                                    const char* const message, X* const field) {
  return [timeout, message, field](fit::function<void()> cont) {
    field->Teardown(timeout, [message, cont = std::move(cont)] {
      if (message) {
        FX_DLOGS(INFO) << "- " << message << " down.";
      }
      cont();
    });
  };
}

}  // namespace

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
    std::string session_id, fuchsia::modular::session::AppConfig session_shell_config,
    fuchsia::modular::session::AppConfig story_shell_config,
    bool use_session_shell_for_story_shell_factory,
    fidl::InterfaceHandle<fuchsia::modular::internal::SessionContext> session_context,
    fuchsia::ui::views::ViewToken view_token) {
  FX_LOGS(INFO) << "SessionmgrImpl::Initialize() called.";

  session_context_ = session_context.Bind();
  OnTerminate(Reset(&session_context_));

  InitializeSessionEnvironment(session_id);
  InitializeAgentRunner(session_shell_config.url());
  InitializeSessionShell(std::move(session_shell_config), std::move(view_token));
  InitializeIntlPropertyProvider();

  InitializeModular(std::move(story_shell_config), use_session_shell_for_story_shell_factory);
  ConnectSessionShellToStoryProvider();
  ReportEvent(ModularLifetimeEventsMetricDimensionEventType::BootedToSessionMgr);
}

void SessionmgrImpl::ConnectSessionShellToStoryProvider() {
  fuchsia::modular::SessionShellPtr session_shell;
  ConnectToSessionShellService(session_shell.NewRequest());
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

  OnTerminate(Reset(&session_environment_));
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

void SessionmgrImpl::InitializeAgentRunner(std::string session_shell_url) {
  startup_agent_launcher_.reset(new StartupAgentLauncher(
      [this](fidl::InterfaceRequest<fuchsia::modular::PuppetMaster> request) {
        if (terminating_) {
          return;
        }
        puppet_master_impl_->Connect(std::move(request));
      },
      [this](fidl::InterfaceRequest<fuchsia::modular::SessionRestartController> request) {
        if (terminating_) {
          return;
        }
        session_restart_controller_bindings_.AddBinding(this, std::move(request));
      },
      [this](fidl::InterfaceRequest<fuchsia::intl::PropertyProvider> request) {
        if (terminating_) {
          return;
        }
        sessionmgr_context_->svc()->Connect<fuchsia::intl::PropertyProvider>(std::move(request));
      },
      [this]() { return terminating_; }));
  OnTerminate(Reset(&startup_agent_launcher_));

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
  std::map<std::string, std::string> agent_service_index;
  for (auto& entry : config_.agent_service_index()) {
    agent_service_index.emplace(entry.service_name(), entry.agent_url());
  }

  ArgvInjectingLauncher::ArgvMap argv_map;
  for (auto& component : config_.component_args()) {
    argv_map.insert(std::make_pair(component.url(), component.args()));
  }
  agent_runner_launcher_ = std::make_unique<ArgvInjectingLauncher>(
      sessionmgr_context_->svc()->Connect<fuchsia::sys::Launcher>(), argv_map);
  auto restart_session_on_agent_crash = config_.restart_session_on_agent_crash();
  restart_session_on_agent_crash.push_back(session_shell_url);
  agent_runner_.reset(new AgentRunner(
      agent_runner_launcher_.get(), startup_agent_launcher_.get(), &inspect_root_node_,
      /*on_critical_agent_crash=*/[this] { RestartDueToCriticalFailure(); },
      std::move(agent_service_index), config_.session_agents(), restart_session_on_agent_crash,
      sessionmgr_context_));
  OnTerminate(Teardown(kAgentRunnerTimeout, "AgentRunner", &agent_runner_));
}

void SessionmgrImpl::InitializeModular(fuchsia::modular::session::AppConfig story_shell_config,
                                       bool use_session_shell_for_story_shell_factory) {
  ComponentContextInfo component_context_info{agent_runner_.get(), config_.session_agents()};

  startup_agent_launcher_->StartAgents(agent_runner_.get(), config_.session_agents(),
                                       config_.startup_agents());

  // The StoryShellFactory to use when creating story shells, or nullptr if no
  // such factory exists.
  fidl::InterfacePtr<fuchsia::modular::StoryShellFactory> story_shell_factory_ptr;

  if (use_session_shell_for_story_shell_factory) {
    ConnectToSessionShellService(story_shell_factory_ptr.NewRequest());
  }

  // We create |story_provider_impl_| after |agent_runner_| so
  // story_provider_impl_ is terminated before agent_runner_, which will cause
  // all modules to be terminated before agents are terminated. Agents must
  // outlive the stories which contain modules that are connected to those
  // agents.

  session_storage_ = std::make_unique<SessionStorage>();
  OnTerminate(Reset(&session_storage_));

  story_provider_impl_.reset(new StoryProviderImpl(
      session_environment_.get(), session_storage_.get(), std::move(story_shell_config),
      std::move(story_shell_factory_ptr), component_context_info, startup_agent_launcher_.get(),
      &inspect_root_node_));
  OnTerminate(Teardown(kStoryProviderTimeout, "StoryProvider", &story_provider_impl_));

  story_command_executor_ = MakeProductionStoryCommandExecutor(session_storage_.get());
  puppet_master_impl_ =
      std::make_unique<PuppetMasterImpl>(session_storage_.get(), story_command_executor_.get());

  session_ctl_ = std::make_unique<SessionCtl>(sessionmgr_context_->outgoing()->debug_dir(),
                                              kSessionCtlDir, puppet_master_impl_.get());

  OnTerminate(Reset(&story_command_executor_));
  OnTerminate(Reset(&puppet_master_impl_));
  OnTerminate(Reset(&session_ctl_));
}

void SessionmgrImpl::InitializeSessionShell(
    fuchsia::modular::session::AppConfig session_shell_config,
    fuchsia::ui::views::ViewToken view_token) {
  session_shell_url_ = session_shell_config.url();
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

void SessionmgrImpl::RunSessionShell(fuchsia::modular::session::AppConfig session_shell_config) {
  ComponentContextInfo component_context_info{agent_runner_.get(), config_.session_agents()};
  session_shell_component_context_impl_ = std::make_unique<ComponentContextImpl>(
      component_context_info, session_shell_url_, session_shell_url_);
  OnTerminate(Reset(&session_shell_component_context_impl_));

  // |service_list| enumerates which services are made available to the session
  // shell.
  auto service_list = fuchsia::sys::ServiceList::New();
  for (auto service_name : agent_runner_->GetAgentServices()) {
    service_list->names.push_back(service_name);
  }

  agent_runner_->PublishAgentServices(session_shell_url_, &session_shell_services_);

  service_list->names.push_back(fuchsia::modular::SessionShellContext::Name_);
  session_shell_services_.AddService<fuchsia::modular::SessionShellContext>([this](auto request) {
    if (terminating_) {
      return;
    }
    session_shell_context_bindings_.AddBinding(this, std::move(request));
  });

  service_list->names.push_back(fuchsia::modular::ComponentContext::Name_);
  session_shell_services_.AddService<fuchsia::modular::ComponentContext>([this](auto request) {
    if (terminating_) {
      return;
    }
    session_shell_component_context_impl_->Connect(std::move(request));
  });

  service_list->names.push_back(fuchsia::modular::PuppetMaster::Name_);
  session_shell_services_.AddService<fuchsia::modular::PuppetMaster>([this](auto request) {
    if (terminating_) {
      return;
    }
    puppet_master_impl_->Connect(std::move(request));
  });

  // The services in |session_shell_services_| are provided through the
  // connection held in |session_shell_service_provider| connected to
  // |session_shell_services_|.
  {
    fuchsia::sys::ServiceProviderPtr session_shell_service_provider;
    session_shell_services_.AddBinding(session_shell_service_provider.NewRequest());
    service_list->provider = std::move(session_shell_service_provider);
  }

  auto session_shell_app = std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
      session_environment_->GetLauncher(), std::move(session_shell_config),
      /* data_origin = */ "", std::move(service_list));

  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  fuchsia::ui::app::ViewProviderPtr view_provider;
  session_shell_app->services().ConnectToService(view_provider.NewRequest());
  scenic::ViewRefPair view_ref_pair = scenic::ViewRefPair::New();
  view_provider->CreateViewWithViewRef(std::move(view_token.value),
                                       std::move(view_ref_pair.control_ref),
                                       std::move(view_ref_pair.view_ref));
  session_shell_view_host_->ConnectView(std::move(view_holder_token));

  agent_runner_->AddRunningAgent(session_shell_url_, std::move(session_shell_app));
}

void SessionmgrImpl::Terminate(fit::function<void()> done) {
  FX_LOGS(INFO) << "Sessionmgr::Terminate()";
  terminating_ = true;
  terminate_done_ = std::move(done);

  TerminateRecurse(on_terminate_cbs_.size() - 1);
}

void SessionmgrImpl::GetComponentContext(
    fidl::InterfaceRequest<fuchsia::modular::ComponentContext> request) {
  session_shell_component_context_impl_->Connect(std::move(request));
}

void SessionmgrImpl::GetPresentation(
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) {
  session_context_->GetPresentation(std::move(request));
}

void SessionmgrImpl::GetStoryProvider(
    fidl::InterfaceRequest<fuchsia::modular::StoryProvider> request) {
  story_provider_impl_->Connect(std::move(request));
}

void SessionmgrImpl::Logout() { Restart(); }

void SessionmgrImpl::Restart() { session_context_->Restart(); }

void SessionmgrImpl::RestartDueToCriticalFailure() {
  session_context_->RestartDueToCriticalFailure();
}

void SessionmgrImpl::OnTerminate(fit::function<void(fit::function<void()>)> action) {
  on_terminate_cbs_.emplace_back(std::move(action));
}

void SessionmgrImpl::TerminateRecurse(const int i) {
  if (i >= 0) {
    on_terminate_cbs_[i]([this, i] { TerminateRecurse(i - 1); });
  } else {
    FX_LOGS(INFO) << "Sessionmgr::Terminate(): done";
    terminate_done_();
  }
}

}  // namespace modular
