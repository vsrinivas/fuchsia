// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/basemgr_impl.h"

#include <fuchsia/ui/app/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/fit/bridge.h>
#include <lib/fostr/fidl/fuchsia/modular/session/formatting.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <zxtest/zxtest.h>

#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/lib/common/async_holder.h"
#include "src/modular/lib/common/teardown.h"
#include "src/modular/lib/fidl/app_client.h"
#include "src/modular/lib/fidl/clone.h"
#include "src/modular/lib/modular_config/modular_config.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"

namespace modular {

// Timeout for tearing down the session launcher component.
static constexpr auto kSessionLauncherComponentTimeout = zx::sec(1);

using cobalt_registry::ModularLifetimeEventsMetricDimensionEventType;

// Implementation of the |fuchsia::modular::session::Launcher| protocol.
class LauncherImpl : public fuchsia::modular::session::Launcher {
 public:
  explicit LauncherImpl(modular::BasemgrImpl* basemgr_impl) : basemgr_impl_(basemgr_impl) {}

  // |Launcher|
  void LaunchSessionmgr(fuchsia::mem::Buffer config) override {
    LaunchSessionmgrWithServices(std::move(config), fuchsia::sys::ServiceList());
  }

  // |Launcher|
  void LaunchSessionmgrWithServices(fuchsia::mem::Buffer config,
                                    fuchsia::sys::ServiceList additional_services) override {
    if (additional_services.names.size() > 0 && !additional_services.host_directory) {
      FX_LOGS(ERROR)
          << "LaunchSessionmgrWithServices() requires additional_servicces.host_directory";
      binding_->Close(ZX_ERR_INVALID_ARGS);
      return;
    }
    FX_DCHECK(binding_);

    if (basemgr_impl_->state() == BasemgrImpl::State::SHUTTING_DOWN) {
      binding_->Close(ZX_ERR_BAD_STATE);
      return;
    }

    // Read the configuration from the buffer.
    std::string config_str;
    if (auto is_read_ok = fsl::StringFromVmo(config, &config_str); !is_read_ok) {
      binding_->Close(ZX_ERR_INVALID_ARGS);
      return;
    }

    // Parse the configuration.
    auto config_result = modular::ParseConfig(config_str);
    if (config_result.is_error()) {
      binding_->Close(ZX_ERR_INVALID_ARGS);
      return;
    }

    // The configuration cannot try to override the session launcher component.
    if (config_result.value().has_basemgr_config() &&
        config_result.value().basemgr_config().has_session_launcher()) {
      binding_->Close(ZX_ERR_INVALID_ARGS);
      return;
    }

    basemgr_impl_->LaunchSessionmgr(config_result.take_value(), std::move(additional_services));
  }

  void set_binding(modular::BasemgrImpl::LauncherBinding* binding) { binding_ = binding; }

  DISALLOW_COPY_ASSIGN_AND_MOVE(LauncherImpl);

 private:
  modular::BasemgrImpl* basemgr_impl_;                        // Not owned.
  modular::BasemgrImpl::LauncherBinding* binding_ = nullptr;  // Not owned.
};

BasemgrImpl::BasemgrImpl(modular::ModularConfigAccessor config_accessor,
                         std::shared_ptr<sys::ServiceDirectory> incoming_services,
                         std::shared_ptr<sys::OutgoingDirectory> outgoing_services,
                         fuchsia::sys::LauncherPtr launcher,
                         fuchsia::ui::policy::PresenterPtr presenter,
                         fuchsia::hardware::power::statecontrol::AdminPtr device_administrator,
                         fit::function<void()> on_shutdown)
    : config_accessor_(std::move(config_accessor)),
      component_context_services_(std::move(incoming_services)),
      outgoing_services_(std::move(outgoing_services)),
      launcher_(std::move(launcher)),
      presenter_(std::move(presenter)),
      device_administrator_(std::move(device_administrator)),
      on_shutdown_(std::move(on_shutdown)),
      session_provider_("SessionProvider"),
      executor_(async_get_default_dispatcher()) {
  outgoing_services_->AddPublicService<fuchsia::modular::Lifecycle>(
      lifecycle_bindings_.GetHandler(this));
  outgoing_services_->AddPublicService(process_lifecycle_bindings_.GetHandler(this),
                                       "fuchsia.process.lifecycle.Lifecycle");

  // Bind the |Launcher| protocol to a client-specific implementation that delegates back to |this|.
  fidl::InterfaceRequestHandler<fuchsia::modular::session::Launcher> launcher_handler =
      [this](fidl::InterfaceRequest<fuchsia::modular::session::Launcher> request) {
        auto impl = std::make_unique<LauncherImpl>(this);
        session_launcher_bindings_.AddBinding(std::move(impl), std::move(request),
                                              /*dispatcher=*/nullptr);
        const auto& binding = session_launcher_bindings_.bindings().back().get();
        binding->impl()->set_binding(binding);
      };
  session_launcher_component_service_dir_.AddEntry(
      fuchsia::modular::session::Launcher::Name_,
      std::make_unique<vfs::Service>(std::move(launcher_handler)));

  Start();
}

BasemgrImpl::~BasemgrImpl() = default;

void BasemgrImpl::Connect(
    fidl::InterfaceRequest<fuchsia::modular::internal::BasemgrDebug> request) {
  basemgr_debug_bindings_.AddBinding(this, std::move(request));
}

fit::promise<void, zx_status_t> BasemgrImpl::StopScenic() {
  fit::bridge<void, zx_status_t> bridge;

  if (!presenter_) {
    FX_LOGS(INFO) << "StopScenic: no presenter; assuming that Scenic has not been launched";
    bridge.completer.complete_ok();
    return bridge.consumer.promise();
  }

  // Lazily connect to lifecycle controller, instead of keeping open an often-unused channel.
  component_context_services_->Connect(scenic_lifecycle_controller_.NewRequest());
  scenic_lifecycle_controller_->Terminate();

  scenic_lifecycle_controller_.set_error_handler(
      [completer = std::move(bridge.completer)](zx_status_t status) mutable {
        if (status == ZX_ERR_PEER_CLOSED) {
          completer.complete_ok();
        } else {
          completer.complete_error(status);
        }
      });

  return bridge.consumer.promise();
}

void BasemgrImpl::Start() {
  ReportEvent(ModularLifetimeEventsMetricDimensionEventType::BootedToBaseMgr);

  if (config_accessor_.basemgr_config().has_session_launcher()) {
    StartSessionLauncherComponent();
  } else {
    CreateSessionProvider(&config_accessor_, fuchsia::sys::ServiceList());

    auto start_session_result = StartSession();
    if (start_session_result.is_error()) {
      FX_PLOGS(FATAL, start_session_result.error()) << "Could not start session";
    }
  }
}

void BasemgrImpl::Shutdown() {
  FX_LOGS(INFO) << "Shutting down basemgr";

  // Prevent the shutdown sequence from running twice.
  if (state_ == State::SHUTTING_DOWN) {
    return;
  }

  state_ = State::SHUTTING_DOWN;

  // Teardown the session provider if it exists.
  // Always completes successfully.
  auto teardown_session_provider = [this]() {
    auto bridge = fit::bridge();
    if (session_provider_.get()) {
      session_provider_.Teardown(kSessionProviderTimeout, bridge.completer.bind());
    } else {
      bridge.completer.complete_ok();
    }
    return bridge.consumer.promise();
  };

  // Teardown the session component if it exists.
  // Always completes successfully.
  auto teardown_session_component_app = [this]() {
    auto bridge = fit::bridge();
    if (session_launcher_component_app_) {
      session_launcher_component_app_->Teardown(kSessionLauncherComponentTimeout,
                                                bridge.completer.bind());
    } else {
      bridge.completer.complete_ok();
    }
    return bridge.consumer.promise();
  };

  // Always completes successfully.
  auto stop_scenic = [this]() {
    return StopScenic().then([](const fit::result<void, zx_status_t>& result) {
      if (result.is_error()) {
        FX_PLOGS(ERROR, result.error())
            << "Scenic LifecycleController experienced some error other than PEER_CLOSED";
      } else {
        FX_DLOGS(INFO) << "- fuchsia::ui::Scenic down";
      }
    });
  };

  auto shutdown = teardown_session_provider()
                      .and_then(teardown_session_component_app())
                      .and_then(stop_scenic())
                      .and_then([this]() {
                        basemgr_debug_bindings_.CloseAll(ZX_OK);
                        on_shutdown_();
                      });

  executor_.schedule_task(std::move(shutdown));
}

void BasemgrImpl::Terminate() { Shutdown(); }

void BasemgrImpl::Stop() { Shutdown(); }

void BasemgrImpl::CreateSessionProvider(const ModularConfigAccessor* const config_accessor,
                                        fuchsia::sys::ServiceList services_from_session_launcher) {
  FX_DCHECK(!session_provider_.get());

  session_provider_.reset(new SessionProvider(
      /* delegate= */ this, launcher_.get(), device_administrator_.get(), config_accessor,
      std::move(services_from_session_launcher),
      /* on_zero_sessions= */
      [this] {
        if (state_ == State::SHUTTING_DOWN) {
          return;
        }
        FX_DLOGS(INFO) << "Re-starting due to session closure";
        auto start_session_result = StartSession();
        if (start_session_result.is_error()) {
          FX_PLOGS(FATAL, start_session_result.error()) << "Could not restart session";
        }
      }));
}

BasemgrImpl::StartSessionResult BasemgrImpl::StartSession() {
  if (state_ == State::SHUTTING_DOWN || !session_provider_.get() ||
      session_provider_->is_session_running()) {
    return fit::error(ZX_ERR_BAD_STATE);
  }

  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto start_session_result = session_provider_->StartSession(std::move(view_token));
  FX_CHECK(start_session_result.is_ok());

  // TODO(fxbug.dev/56132): Ownership of the Presenter should be moved to the session shell.
  if (presenter_) {
    presentation_container_ =
        std::make_unique<PresentationContainer>(presenter_.get(), std::move(view_holder_token));
    presenter_.set_error_handler(
        [this](zx_status_t /* unused */) { presentation_container_.reset(); });
  }

  return fit::ok();
}

void BasemgrImpl::RestartSession(RestartSessionCallback on_restart_complete) {
  if (state_ == State::SHUTTING_DOWN || !session_provider_.get()) {
    return;
  }
  session_provider_->RestartSession(std::move(on_restart_complete));
}

void BasemgrImpl::StartSessionWithRandomId() {
  // If there is a session already running, exit.
  if (session_provider_.get()) {
    return;
  }
  FX_CHECK(!session_provider_.get());

  // The new session uses a configuration based on its existing configuration,
  // with an argument set that ensures it starts with a random session ID.
  //
  // Create a copy of the configuration that ensures a random session ID is used.
  // TODO(fxbug.dev/51752): Create a config field for use_random_session_id and remove base shell
  auto new_config = CloneStruct(config_accessor_.config());
  new_config.mutable_basemgr_config()
      ->mutable_base_shell()
      ->mutable_app_config()
      ->mutable_args()
      ->push_back(modular_config::kPersistUserArg);

  // Set the new config and create a session provider.
  //
  // Overwrite the config accessor that was the source for the original configuration,
  // and which will be used to launch sessions in the future.
  //
  // This method, StartSessionWithRandomId(), is defined on the BasemgrDebug interface.
  // It only ever launches a new session, and thus will use the default config.
  config_accessor_ = ModularConfigAccessor(std::move(new_config));
  CreateSessionProvider(&config_accessor_, fuchsia::sys::ServiceList());

  if (auto result = StartSession(); result.is_error()) {
    FX_PLOGS(ERROR, result.error()) << "Could not start session";
  }
}

void BasemgrImpl::GetPresentation(
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) {
  if (!presentation_container_) {
    request.Close(ZX_ERR_NOT_FOUND);
    return;
  }
  presentation_container_->GetPresentation(std::move(request));
}

void BasemgrImpl::LaunchSessionmgr(fuchsia::modular::session::ModularConfig config,
                                   fuchsia::sys::ServiceList services_from_session_launcher) {
  // If there is a session provider, tear it down and try again. This stops any running session.
  if (session_provider_.get()) {
    session_provider_.Teardown(kSessionProviderTimeout,
                               [this, config = std::move(config),
                                services = std::move(services_from_session_launcher)]() mutable {
                                 session_provider_.reset(nullptr);
                                 LaunchSessionmgr(std::move(config), std::move(services));
                               });
    return;
  }

  launch_sessionmgr_config_accessor_ =
      std::make_unique<modular::ModularConfigAccessor>(std::move(config));

  CreateSessionProvider(launch_sessionmgr_config_accessor_.get(),
                        std::move(services_from_session_launcher));

  if (auto result = StartSession(); result.is_error()) {
    FX_PLOGS(ERROR, result.error()) << "Could not start session";
  }
}

void BasemgrImpl::StartSessionLauncherComponent() {
  auto app_config = CloneStruct(config_accessor_.basemgr_config().session_launcher());

  auto services = CreateAndServeSessionLauncherComponentServices();

  FX_LOGS(INFO) << "Starting session launcher component: " << app_config.url();

  session_launcher_component_app_ = std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
      launcher_.get(), std::move(app_config), /*data_origin=*/"", std::move(services),
      /*flat_namespace=*/nullptr);
}

fuchsia::sys::ServiceListPtr BasemgrImpl::CreateAndServeSessionLauncherComponentServices() {
  fidl::InterfaceHandle<fuchsia::io::Directory> dir_handle;
  session_launcher_component_service_dir_.Serve(
      fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
      dir_handle.NewRequest().TakeChannel());

  auto services = fuchsia::sys::ServiceList::New();
  services->names.push_back(fuchsia::modular::session::Launcher::Name_);
  services->host_directory = dir_handle.TakeChannel();

  return services;
}

}  // namespace modular
