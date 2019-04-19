// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/modular_test_harness/cpp/test_harness_impl.h"

#include <lib/fsl/vmo/strings.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/join_strings.h>
#include <src/lib/fxl/strings/substitute.h>

namespace modular::testing {
namespace {

constexpr char kBasemgrUrl[] =
    "fuchsia-pkg://fuchsia.com/basemgr#meta/basemgr.cmx";

// Defaut shell URLs which are used if not specified.
constexpr char kBaseShellDefaultUrl[] =
    "fuchsia-pkg://fuchsia.com/modular_test_harness#meta/test_base_shell.cmx";
constexpr char kSessionShellDefaultUrl[] =
    "fuchsia-pkg://fuchsia.com/modular_test_harness#meta/"
    "test_session_shell.cmx";
constexpr char kStoryShellDefaultUrl[] =
    "fuchsia-pkg://fuchsia.com/modular_test_harness#meta/test_story_shell.cmx";

constexpr char kSessionAgentFakeInterceptionUrl[] =
    "fuchsia-pkg://example.com/FAKE_SESSION_AGENT_PKG/fake_session_agent.cmx";
constexpr char kSessionAgentFakeInterceptionCmx[] = R"(
{
  "sandbox": {
    "services": [
      "fuchsia.modular.PuppetMaster",
      "fuchsia.modular.AgentContext",
      "fuchsia.modular.ComponentContext"
    ]
  }
}
)";

};  // namespace

class TestHarnessImpl::InterceptedComponentImpl
    : public fuchsia::modular::testing::InterceptedComponent {
 public:
  using RemoveHandler = fit::function<void()>;
  InterceptedComponentImpl(
      std::unique_ptr<sys::testing::InterceptedComponent> impl,
      fidl::InterfaceRequest<fuchsia::modular::testing::InterceptedComponent>
          request)
      : impl_(std::move(impl)), binding_(this, std::move(request)) {
    impl_->set_on_kill([this] {
      binding_.events().OnKill();
      remove_handler_();
    });
  }

  virtual ~InterceptedComponentImpl() = default;

  void set_remove_handler(RemoveHandler remove_handler) {
    remove_handler_ = std::move(remove_handler);
  }

 private:
  // |fuchsia::modular::testing::InterceptedComponent|
  void Exit(int64_t exit_code, fuchsia::sys::TerminationReason reason) {
    impl_->Exit(exit_code, reason);
    remove_handler_();
  }

  std::unique_ptr<sys::testing::InterceptedComponent> impl_;
  fidl::Binding<fuchsia::modular::testing::InterceptedComponent> binding_;
  RemoveHandler remove_handler_;
};

// This class implements a session agent using AgentDriver.
class TestHarnessImpl::InterceptedSessionAgent final {
 public:
  InterceptedSessionAgent(::modular::AgentHost* host) {}

  // Called by AgentDriver.
  void Connect(
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> outgoing_services) {
  }

  // Called by AgentDriver.
  void RunTask(const fidl::StringPtr& task_id,
               const fit::function<void()>& done) {
    FXL_DLOG(WARNING) << "This session agent does not run tasks";
    done();
  }

  // Called by AgentDriver.
  void Terminate(const fit::function<void()>& done) { done(); }
};

TestHarnessImpl::TestHarnessImpl(
    const fuchsia::sys::EnvironmentPtr& parent_env,
    fidl::InterfaceRequest<fuchsia::modular::testing::TestHarness> request,
    fit::function<void()> on_disconnected)
    : parent_env_(parent_env),
      binding_(this, std::move(request)),
      on_disconnected_(std::move(on_disconnected)),
      interceptor_(
          sys::testing::ComponentInterceptor::CreateWithEnvironmentLoader(
              parent_env_)) {
  binding_.set_error_handler(
      [this](zx_status_t status) { CloseBindingIfError(status); });
}

TestHarnessImpl::~TestHarnessImpl() = default;

void TestHarnessImpl::GetService(
    fuchsia::modular::testing::TestHarnessService service) {
  switch (service.Which()) {
    case fuchsia::modular::testing::TestHarnessService::Tag::kPuppetMaster: {
      BufferSessionAgentService(std::move(service.puppet_master()));
    } break;

    case fuchsia::modular::testing::TestHarnessService::Tag::
        kComponentContext: {
      BufferSessionAgentService(std::move(service.component_context()));
    } break;

    case fuchsia::modular::testing::TestHarnessService::Tag::kAgentContext: {
      BufferSessionAgentService(std::move(service.agent_context()));
    } break;

    case fuchsia::modular::testing::TestHarnessService::Tag::Empty: {
      FXL_LOG(ERROR) << "The given TestHarnessService is empty.";
      CloseBindingIfError(ZX_ERR_INVALID_ARGS);
      return;
    } break;
  }

  FlushBufferedSessionAgentServices();
}

bool TestHarnessImpl::CloseBindingIfError(zx_status_t status) {
  if (status != ZX_OK) {
    binding_.Close(status);
    // destory |enclosing_env_| should kill all processes.
    enclosing_env_.reset();
    on_disconnected_();
    return true;
  }
  return false;
}

void TestHarnessImpl::Run(fuchsia::modular::testing::TestHarnessSpec spec) {
  // Run() can only be called once.
  if (enclosing_env_) {
    CloseBindingIfError(ZX_ERR_ALREADY_BOUND);
    return;
  }

  spec_ = std::move(spec);

  if (CloseBindingIfError(SetupBaseShellInterception())) {
    return;
  }
  if (CloseBindingIfError(SetupSessionShellInterception())) {
    return;
  }
  if (CloseBindingIfError(SetupStoryShellInterception())) {
    return;
  }
  if (CloseBindingIfError(SetupComponentInterception())) {
    return;
  }
  if (CloseBindingIfError(SetupFakeSessionAgent())) {
    return;
  }

  std::unique_ptr<sys::testing::EnvironmentServices> env_services =
      interceptor_.MakeEnvironmentServices(parent_env_);

  // Allow services to be inherited from outside the test harness environment.
  if (spec_.has_env_services_to_inherit()) {
    for (auto& svc_name : spec_.env_services_to_inherit()) {
      env_services->AllowParentService(svc_name);
    }
  }
  // Add account manager and device settings manager which by basemgr has hard
  // dependencies on.
  env_services->AllowParentService(
      fuchsia::auth::account::AccountManager::Name_);
  env_services->AllowParentService(
      fuchsia::devicesettings::DeviceSettingsManager::Name_);
  enclosing_env_ = sys::testing::EnclosingEnvironment::Create(
      "modular_test_harness", parent_env_, std::move(env_services));

  fuchsia::sys::LaunchInfo info;
  info.url = kBasemgrUrl;
  info.arguments = fidl::VectorPtr(MakeBasemgrArgs(spec_));
  basemgr_ctrl_ = enclosing_env_->CreateComponent(std::move(info));
}

zx_status_t TestHarnessImpl::SetupFakeSessionAgent() {
  auto interception_retval = interceptor_.InterceptURL(
      kSessionAgentFakeInterceptionUrl, kSessionAgentFakeInterceptionCmx,
      [this](fuchsia::sys::StartupInfo startup_info,
             std::unique_ptr<sys::testing::InterceptedComponent>
                 intercepted_component) {
        intercepted_session_agent_info_.component_context =
            component::StartupContext::CreateFrom(std::move(startup_info));
        intercepted_session_agent_info_.agent_driver.reset(
            new ::modular::AgentDriver<InterceptedSessionAgent>(
                intercepted_session_agent_info_.component_context.get(),
                [] {}));
        intercepted_session_agent_info_.intercepted_component =
            std::move(intercepted_component);

        FlushBufferedSessionAgentServices();
      });

  if (!interception_retval) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

std::string ParseShellSpec(
    const fuchsia::modular::testing::ShellSpec& shell_spec) {
  if (shell_spec.is_component_url()) {
    return shell_spec.component_url();
  }
  return shell_spec.intercept_spec().component_url();
}

// static
std::vector<std::string> TestHarnessImpl::MakeBasemgrArgs(
    const fuchsia::modular::testing::TestHarnessSpec& const_spec) {
  fuchsia::modular::testing::TestHarnessSpec spec;
  const_spec.Clone(&spec);

  std::string base_shell_url;
  std::string session_shell_url;
  std::string story_shell_url;

  // 1. Figure out the base, session & story shell URLs.
  base_shell_url = spec.has_base_shell() ? ParseShellSpec(spec.base_shell())
                                         : kBaseShellDefaultUrl;
  session_shell_url = spec.has_session_shell()
                          ? ParseShellSpec(spec.session_shell())
                          : kSessionShellDefaultUrl;
  story_shell_url = spec.has_story_shell() ? ParseShellSpec(spec.story_shell())
                                           : kStoryShellDefaultUrl;

  // 2. Figure out sessionmgr args.
  std::vector<std::string> sessionmgr_args;
  std::set<std::string> session_agents = {kSessionAgentFakeInterceptionUrl};

  // Empty intiialize sessionmgr config if it isn't set, so we can continue to
  // use it for default-initializing some fields below.
  if (!spec.has_sessionmgr_config()) {
    spec.set_sessionmgr_config(fuchsia::modular::session::SessionmgrConfig{});
  }

  if (!spec.sessionmgr_config().has_use_memfs_for_ledger() ||
      spec.sessionmgr_config().use_memfs_for_ledger()) {
    sessionmgr_args.push_back("--use_memfs_for_ledger");
  }
  if (!spec.sessionmgr_config().has_cloud_provider() ||
      spec.sessionmgr_config().cloud_provider() ==
          fuchsia::modular::session::CloudProvider::NONE) {
    sessionmgr_args.push_back("--no_cloud_provider_for_ledger");
  }
  if (spec.sessionmgr_config().has_session_agents()) {
    session_agents.insert(spec.sessionmgr_config().session_agents().begin(),
                          spec.sessionmgr_config().session_agents().end());
  }
  sessionmgr_args.push_back("--session_agents=" +
                            fxl::JoinStrings(session_agents, "\\\\,"));

  std::vector<std::string> args;
  // Be default, we use the --test flag.
  args.push_back("--test");
  args.push_back(fxl::Substitute("--base_shell=$0", base_shell_url));
  args.push_back(fxl::Substitute("--session_shell=$0", session_shell_url));
  args.push_back(fxl::Substitute("--story_shell=$0", story_shell_url));
  args.push_back("--sessionmgr_args=" + fxl::JoinStrings(sessionmgr_args, ","));

  return args;
}

fuchsia::modular::testing::InterceptedComponentPtr
TestHarnessImpl::AddInterceptedComponentBinding(
    std::unique_ptr<sys::testing::InterceptedComponent> intercepted_component) {
  fuchsia::modular::testing::InterceptedComponentPtr ptr;
  auto impl = std::make_unique<InterceptedComponentImpl>(
      std::move(intercepted_component), ptr.NewRequest());

  // Hold on to |impl|.
  // Automatically remove/destroy |impl| if its associated binding closes.
  auto key = impl.get();
  impl->set_remove_handler(
      [this, key] { intercepted_component_impls_.erase(key); });
  intercepted_component_impls_[key] = std::move(impl);

  return ptr;
}

zx_status_t TestHarnessImpl::SetupBaseShellInterception() {
  if (!spec_.has_base_shell() || !spec_.base_shell().is_intercept_spec()) {
    return ZX_OK;
  }
  if (auto retval = SetupShellInterception(
          spec_.base_shell(),
          [this](fuchsia::sys::StartupInfo info,
                 std::unique_ptr<sys::testing::InterceptedComponent>
                     intercepted_component) {
            binding_.events().OnNewBaseShell(
                std::move(info), AddInterceptedComponentBinding(
                                     std::move(intercepted_component)));
          });
      retval != ZX_OK) {
    FXL_LOG(ERROR)
        << "Could not process base shell configuration. "
           "TestHarnessSpec.base_shell must be set to a valid ShellSpec.";
    return retval;
  }
  return ZX_OK;
}

zx_status_t TestHarnessImpl::SetupSessionShellInterception() {
  if (!spec_.has_session_shell() ||
      !spec_.session_shell().is_intercept_spec()) {
    return ZX_OK;
  }
  if (auto retval =
          SetupShellInterception(
              spec_.session_shell(),
              [this](fuchsia::sys::StartupInfo info,
                     std::unique_ptr<sys::testing::InterceptedComponent>
                         intercepted_component) {
                binding_.events().OnNewSessionShell(
                    std::move(info), AddInterceptedComponentBinding(
                                         std::move(intercepted_component)));
              }) != ZX_OK) {
    FXL_LOG(ERROR)
        << "Could not process session shell configuration. "
           "TestHarnessSpec.session_shell must be set to a valid ShellSpec.";
    return retval;
  }
  return ZX_OK;
}

zx_status_t TestHarnessImpl::SetupStoryShellInterception() {
  if (!spec_.has_story_shell() || !spec_.story_shell().is_intercept_spec()) {
    return ZX_OK;
  }
  if (auto retval =
          SetupShellInterception(
              spec_.story_shell(),
              [this](fuchsia::sys::StartupInfo info,
                     std::unique_ptr<sys::testing::InterceptedComponent>
                         intercepted_component) {
                binding_.events().OnNewStoryShell(
                    std::move(info), AddInterceptedComponentBinding(
                                         std::move(intercepted_component)));
              }) != ZX_OK) {
    FXL_LOG(ERROR)
        << "Could not process story shell configuration. "
           "TestHarnessSpec.story_shell must be set to a valid ShellSpec.";
    return retval;
  }
  return ZX_OK;
}

std::string GetCmxAsString(
    const fuchsia::modular::testing::InterceptSpec& intercept_spec) {
  std::string cmx_str = "";
  if (intercept_spec.has_extra_cmx_contents()) {
    if (!fsl::StringFromVmo(intercept_spec.extra_cmx_contents(), &cmx_str)) {
      // Not returning |cmx_str| since fsl::StringFromVmo doesn't guarantee that
      // |cmx_str| will be untouched on failure.
      return "";
    }
  }

  return cmx_str;
}

// Returns `false` if the supplied |shell_spec| is
zx_status_t TestHarnessImpl::SetupShellInterception(
    const fuchsia::modular::testing::ShellSpec& shell_spec,
    sys::testing::ComponentInterceptor::ComponentLaunchHandler
        fake_interception_callback) {
  switch (shell_spec.Which()) {
    case fuchsia::modular::testing::ShellSpec::Tag::kInterceptSpec: {
      if (!interceptor_.InterceptURL(
              shell_spec.intercept_spec().component_url(),
              GetCmxAsString(shell_spec.intercept_spec()),
              std::move(fake_interception_callback))) {
        return ZX_ERR_INVALID_ARGS;
      }
    } break;

    case fuchsia::modular::testing::ShellSpec::Tag::kComponentUrl:
      // Consumed by |MakeBasemgrArgs()|.
      break;

    case fuchsia::modular::testing::ShellSpec::Tag::Empty: {
      FXL_LOG(WARNING) << "Unset ShellSpec value.";
      return ZX_ERR_INVALID_ARGS;
    } break;
  }

  return ZX_OK;
}

zx_status_t TestHarnessImpl::SetupComponentInterception() {
  if (!spec_.has_components_to_intercept()) {
    return ZX_OK;
  }
  for (const auto& intercept_spec : spec_.components_to_intercept()) {
    if (!interceptor_.InterceptURL(
            intercept_spec.component_url(), GetCmxAsString(intercept_spec),
            [this](fuchsia::sys::StartupInfo startup_info,
                   std::unique_ptr<sys::testing::InterceptedComponent>
                       intercepted_component) {
              binding_.events().OnNewComponent(
                  std::move(startup_info),
                  AddInterceptedComponentBinding(
                      std::move(intercepted_component)));
            })) {
      return ZX_ERR_INVALID_ARGS;
    }
  }
  return ZX_OK;
}

void TestHarnessImpl::FlushBufferedSessionAgentServices() {
  if (!intercepted_session_agent_info_.component_context) {
    return;
  }

  for (auto&& req : intercepted_session_agent_info_.buffered_service_requests) {
    intercepted_session_agent_info_.component_context
        ->ConnectToEnvironmentService(req.service_name,
                                      std::move(req.service_request));
  }
  intercepted_session_agent_info_.buffered_service_requests.clear();
}

}  // namespace modular::testing
