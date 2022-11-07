// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/basemgr_impl.h"

#include <fuchsia/session/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/promise.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_creation_tokens.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/handle.h>
#include <zircon/status.h>
#include <zircon/time.h>
#include <zircon/types.h>
#include <zircon/utc.h>

#include <variant>

#include "src/lib/files/directory.h"
#include "src/lib/fostr/fidl/fuchsia/session/formatting.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/bin/basemgr/child_listener.h"
#include "src/modular/bin/basemgr/cobalt/metrics_logger.h"
#include "src/modular/lib/common/teardown.h"
#include "src/modular/lib/fidl/app_client.h"
#include "src/modular/lib/fidl/clone.h"
#include "src/modular/lib/modular_config/modular_config.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"

namespace modular {

// Implementation of the |fuchsia::modular::session::Launcher| protocol.
class LauncherImpl : public fuchsia::modular::session::Launcher {
 public:
  explicit LauncherImpl(modular::BasemgrImpl* basemgr_impl) : basemgr_impl_(basemgr_impl) {}

  // |Launcher|
  void LaunchSessionmgr(fuchsia::mem::Buffer config) override {
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

    basemgr_impl_->LaunchSessionmgr(config_result.take_value());
  }

  void set_binding(modular::BasemgrImpl::LauncherBinding* binding) { binding_ = binding; }

  DISALLOW_COPY_ASSIGN_AND_MOVE(LauncherImpl);

 private:
  modular::BasemgrImpl* basemgr_impl_;                        // Not owned.
  modular::BasemgrImpl::LauncherBinding* binding_ = nullptr;  // Not owned.
};

BasemgrImpl::BasemgrImpl(modular::ModularConfigAccessor config_accessor,
                         std::shared_ptr<sys::OutgoingDirectory> outgoing_services,
                         BasemgrInspector* inspector, bool use_flatland,
                         fuchsia::sys::LauncherPtr launcher, SceneOwnerPtr scene_owner,
                         fuchsia::hardware::power::statecontrol::AdminPtr device_administrator,
                         fuchsia::session::RestarterPtr session_restarter,
                         std::unique_ptr<ChildListener> child_listener,
                         std::optional<fuchsia::ui::app::ViewProviderPtr> view_provider,
                         fit::function<void()> on_shutdown)
    : config_accessor_(std::move(config_accessor)),
      outgoing_services_(std::move(outgoing_services)),
      inspector_(inspector),
      launcher_(std::move(launcher)),
      scene_owner_(std::move(scene_owner)),
      child_listener_(std::move(child_listener)),
      device_administrator_(std::move(device_administrator)),
      session_restarter_(std::move(session_restarter)),
      view_provider_(std::move(view_provider)),
      on_shutdown_(std::move(on_shutdown)),
      session_provider_("SessionProvider"),
      executor_(async_get_default_dispatcher()),
      use_flatland_(use_flatland),
      weak_factory_(this) {
  outgoing_services_->AddPublicService<fuchsia::modular::Lifecycle>(
      lifecycle_bindings_.GetHandler(this));
  outgoing_services_->AddPublicService(process_lifecycle_bindings_.GetHandler(this),
                                       "fuchsia.process.lifecycle.Lifecycle");
  outgoing_services_->AddPublicService(GetLauncherHandler(),
                                       fuchsia::modular::session::Launcher::Name_);

  LogLifetimeEvent(
      cobalt_registry::ModularLifetimeEventsMigratedMetricDimensionEventType::BootedToBaseMgr);

  zx::channel lifecycle_request{zx_take_startup_handle(PA_LIFECYCLE)};
  if (lifecycle_request) {
    process_lifecycle_bindings_.AddBinding(
        this, fidl::InterfaceRequest<fuchsia::process::lifecycle::Lifecycle>(
                  std::move(lifecycle_request)));
  }
}

BasemgrImpl::~BasemgrImpl() = default;

void BasemgrImpl::Connect(
    fidl::InterfaceRequest<fuchsia::modular::internal::BasemgrDebug> request) {
  basemgr_debug_bindings_.AddBinding(this, std::move(request));
}

void BasemgrImpl::Start() {
  CreateSessionProvider(&config_accessor_);

  // Start listening to child components if a listener is set.
  if (child_listener_) {
    child_listener_->StartListening(session_restarter_.get());
  }

  auto start_session_result = StartSession();
  if (start_session_result.is_error()) {
    FX_PLOGS(FATAL, start_session_result.error()) << "Could not start session.";
  }
}

void BasemgrImpl::Shutdown() {
  FX_LOGS(INFO) << "Shutting down basemgr.";

  // Prevent the shutdown sequence from running twice.
  if (state_ == State::SHUTTING_DOWN) {
    return;
  }

  state_ = State::SHUTTING_DOWN;

  // Teardown the session provider if it exists.
  // Always completes successfully.
  auto teardown_session_provider = [this]() {
    auto bridge = fpromise::bridge();
    session_provider_.Teardown(kSessionProviderTimeout, bridge.completer.bind());
    return bridge.consumer.promise();
  };

  auto shutdown = teardown_session_provider().and_then([this]() {
    basemgr_debug_bindings_.CloseAll(ZX_OK);
    on_shutdown_();
  });

  executor_.schedule_task(std::move(shutdown));
}

void BasemgrImpl::Terminate() { Shutdown(); }

void BasemgrImpl::Stop() { Shutdown(); }

void BasemgrImpl::CreateSessionProvider(const ModularConfigAccessor* const config_accessor) {
  FX_DCHECK(!session_provider_.get());

  // launch with additional v2 services published in "svc_for_v1_sessionmgr"
  fuchsia::sys::ServiceList svc_for_v1_sessionmgr;
  auto path = std::string("/") + modular_config::kServicesForV1Sessionmgr;
  if (files::IsDirectory(path)) {
    FX_LOGS(INFO) << "Found svc_for_v1_sessionmgr";

    zx_status_t status;
    status = fdio_open(path.c_str(),
                       static_cast<uint32_t>(fuchsia_io::wire::OpenFlags::kRightReadable |
                                             fuchsia_io::wire::OpenFlags::kDirectory |
                                             fuchsia_io::wire::OpenFlags::kRightWritable),
                       svc_for_v1_sessionmgr.host_directory.NewRequest().TakeChannel().release());
    FX_CHECK(status == ZX_OK) << "failed to open " << path << ": " << zx_status_get_string(status);

    std::vector<std::string> v2_services;
    FX_CHECK(files::ReadDirContents(path, &v2_services)) << "failed to read directory: " << path;
    for (const auto& v2_service : v2_services) {
      if (v2_service != ".") {
        FX_LOGS(INFO) << "Found v2 service: " << v2_service;
        svc_for_v1_sessionmgr.names.push_back(v2_service);
      }
    }
  } else {
    FX_LOGS(INFO) << "No svc_for_v1_sessionmgr from v2";
  }

  session_provider_.reset(
      new SessionProvider(launcher_.get(), device_administrator_.get(), config_accessor,
                          std::move(svc_for_v1_sessionmgr), outgoing_services_->root_dir(),
                          /*on_zero_sessions=*/[this] {
                            if (state_ == State::SHUTTING_DOWN || state_ == State::RESTARTING) {
                              return;
                            }
                            FX_DLOGS(INFO) << "Restarting session due to sessionmgr shutdown.";
                            RestartSession([weak_this = weak_factory_.GetWeakPtr()]() {
                              if (weak_this) {
                                weak_this->state_ = State::RUNNING;
                              }
                            });
                          }));

  FX_LOGS(INFO) << "Waiting for clock started signal.";
  auto fp =
      executor_.MakePromiseWaitHandle(zx::unowned_handle(zx_utc_reference_get()), ZX_CLOCK_STARTED)
          .then([this](fpromise::result<zx_packet_signal_t, zx_status_t>& result) {
            if (result.is_error()) {
              FX_LOGS(ERROR) << "System clock failed to send start signal: "
                             << zx_status_get_string(result.take_error());
            } else {
              FX_LOGS(INFO) << "System clock has started.";
              session_provider_->MarkClockAsStarted();
            }
          });
  executor_.schedule_task(fpromise::pending_task(std::move(fp)));
}

BasemgrImpl::StartSessionResult BasemgrImpl::StartSession() {
  if (state_ == State::SHUTTING_DOWN || !session_provider_.get() ||
      session_provider_->is_session_running()) {
    return fpromise::error(ZX_ERR_BAD_STATE);
  }

  if (use_flatland_) {
    fuchsia::session::scene::ManagerPtr* scene_manager =
        std::get_if<fuchsia::session::scene::ManagerPtr>(&scene_owner_);
    FX_CHECK(scene_manager != nullptr);

    auto [view_creation_token, viewport_creation_token] = scenic::ViewCreationTokenPair::New();

    std::optional<ViewParams> view_params = std::nullopt;
    // Get the view from the v2 session shell if available.
    if (view_provider_.has_value()) {
      FX_LOGS(INFO) << "Creating Flatland view for v2 session shell.";
      fuchsia::ui::app::CreateView2Args create_view_args;
      create_view_args.set_view_creation_token(std::move(view_creation_token));
      (*view_provider_)->CreateView2(std::move(create_view_args));
    } else {
      FX_LOGS(INFO)
          << "No ViewProvider, sessionmgr will create Flatland view for v1 session shell.";
      view_params = std::make_optional(std::move(view_creation_token));
    }

    auto start_session_result = session_provider_->StartSession(std::move(view_params));
    FX_CHECK(start_session_result.is_ok());
    inspector_->AddSessionStartedAt(zx_clock_get_monotonic());

    // TODO(fxbug.dev/56132): Ownership of the Presenter should be moved to the session shell.
    scene_manager->set_error_handler([](zx_status_t error) {
      FX_PLOGS(ERROR, error) << "Error on fuchsia.session.scene.Manager.";
    });
    (*scene_manager)->PresentRootView(std::move(viewport_creation_token), [](auto) {});
  } else {
    auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
    scenic::ViewRefPair view_ref_pair = scenic::ViewRefPair::New();
    auto view_ref_clone = fidl::Clone(view_ref_pair.view_ref);

    std::optional<ViewParams> view_params = std::nullopt;
    // Get the view from the v2 session shell if available.
    if (view_provider_.has_value()) {
      FX_LOGS(INFO) << "Creating Gfx view for v2 session shell.";
      (*view_provider_)
          ->CreateViewWithViewRef(std::move(view_token.value), std::move(view_ref_pair.control_ref),
                                  std::move(view_ref_pair.view_ref));
    } else {
      FX_LOGS(INFO) << "No ViewProvider, sessionmgr will create Gfx view for v1 session shell.";
      view_params = std::make_optional(GfxViewParams{.view_token = std::move(view_token),
                                                     .view_ref_pair = std::move(view_ref_pair)});
    }

    auto start_session_result = session_provider_->StartSession(std::move(view_params));
    FX_CHECK(start_session_result.is_ok());
    inspector_->AddSessionStartedAt(zx_clock_get_monotonic());

    fuchsia::session::scene::ManagerPtr* scene_manager =
        std::get_if<fuchsia::session::scene::ManagerPtr>(&scene_owner_);
    fuchsia::ui::policy::PresenterPtr* root_presenter =
        std::get_if<fuchsia::ui::policy::PresenterPtr>(&scene_owner_);
    // TODO(fxbug.dev/56132): Ownership of the Presenter should be moved to the session shell.
    if (scene_manager) {
      scene_manager->set_error_handler([](zx_status_t error) {
        FX_PLOGS(ERROR, error) << "Error on fuchsia.session.scene.Manager.";
      });
      (*scene_manager)
          ->PresentRootViewLegacy(std::move(view_holder_token), std::move(view_ref_clone),
                                  [](auto) {});
    } else if (root_presenter) {
      root_presenter->set_error_handler([this](zx_status_t error) {
        FX_LOGS(ERROR) << "Error on fuchsia.ui.policy.Presenter: " << zx_status_get_string(error);
        presentation_.Unbind();
      });
      (*root_presenter)
          ->PresentOrReplaceView2(std::move(view_holder_token), std::move(view_ref_clone),
                                  presentation_.NewRequest());
    }
  }

  return fpromise::ok();
}

void BasemgrImpl::RestartSession(RestartSessionCallback on_restart_complete) {
  if (state_ == State::SHUTTING_DOWN || state_ == State::RESTARTING || !session_provider_.get()) {
    on_restart_complete();
    return;
  }

  state_ = State::RESTARTING;

  FX_LOGS(INFO) << "Restarting session.";

  session_restarter_.set_error_handler([weak_this = weak_factory_.GetWeakPtr(),
                                        on_restart_complete = on_restart_complete.share()](
                                           zx_status_t status) mutable {
    if (!weak_this) {
      on_restart_complete();
      return;
    }

    FX_PLOGS(WARNING, status)
        << "Lost connection to fuchsia.session.Restarter. "
           "This should only happen when basemgr is running as a v1 component. "
           "Falling back to restarting just sessionmgr.";
    // Shut down the existing session and start a new one, but keep the existing SessionProvider.
    weak_this->session_provider_->Shutdown(
        [weak_this = std::move(weak_this), on_restart_complete = std::move(on_restart_complete)]() {
          if (!weak_this) {
            return;
          }
          auto start_session_result = weak_this->StartSession();
          if (start_session_result.is_error()) {
            FX_PLOGS(FATAL, start_session_result.error()) << "Could not restart session.";
          }
          weak_this->state_ = State::RUNNING;
          on_restart_complete();
        });
  });

  session_restarter_->Restart([weak_this = weak_factory_.GetWeakPtr(),
                               on_restart_complete = std::move(on_restart_complete)](
                                  fuchsia::session::Restarter_Restart_Result result) {
    if (result.is_err()) {
      FX_LOGS(FATAL) << "Failed to restart session: " << result.err();
    }
    if (weak_this) {
      weak_this->state_ = State::RUNNING;
    }
    on_restart_complete();
  });
}

void BasemgrImpl::StartSessionWithRandomId() {
  // If there is a session already running, exit.
  if (session_provider_.get()) {
    return;
  }
  FX_CHECK(!session_provider_.get());

  Start();
}

void BasemgrImpl::LaunchSessionmgr(fuchsia::modular::session::ModularConfig config) {
  state_ = State::RESTARTING;

  // If there is a session provider, tear it down and try again. This stops any running
  // sessionmgr.
  if (session_provider_.get()) {
    session_provider_.Teardown(
        kSessionProviderTimeout,
        [this, config = std::move(config)]() mutable { LaunchSessionmgr(std::move(config)); });
    return;
  }

  launch_sessionmgr_config_accessor_ =
      std::make_unique<modular::ModularConfigAccessor>(std::move(config));

  CreateSessionProvider(launch_sessionmgr_config_accessor_.get());

  if (auto result = StartSession(); result.is_error()) {
    FX_PLOGS(ERROR, result.error()) << "Could not start session";
  }

  state_ = State::RUNNING;
}

fidl::InterfaceRequestHandler<fuchsia::modular::session::Launcher>
BasemgrImpl::GetLauncherHandler() {
  return [this](fidl::InterfaceRequest<fuchsia::modular::session::Launcher> request) {
    auto impl = std::make_unique<LauncherImpl>(this);
    session_launcher_bindings_.AddBinding(std::move(impl), std::move(request),
                                          /*dispatcher=*/nullptr);
    const auto& binding = session_launcher_bindings_.bindings().back().get();
    binding->impl()->set_binding(binding);
  };
}

}  // namespace modular
