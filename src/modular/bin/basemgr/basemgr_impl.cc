// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/basemgr_impl.h"

#include <fuchsia/ui/app/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <zxtest/zxtest.h>

#include "src/lib/fsl/types/type_converters.h"
#include "src/lib/intl/intl_property_provider_impl/intl_property_provider_impl.h"
#include "src/modular/lib/common/async_holder.h"
#include "src/modular/lib/common/teardown.h"
#include "src/modular/lib/fidl/app_client.h"
#include "src/modular/lib/fidl/clone.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"

namespace fidl {
template <>
// fidl::TypeConverter specialization for fuchsia::modular::session::AppConfig
// TODO(MF-277) Convert all usages of fuchsia::modular::AppConfig to
// fuchsia::modular::session::AppConfig and remove this converter.
struct TypeConverter<fuchsia::modular::AppConfig, fuchsia::modular::session::AppConfig> {
  // Converts fuchsia::modular::session::AppConfig to
  // fuchsia::modular::AppConfig
  static fuchsia::modular::AppConfig Convert(const fuchsia::modular::session::AppConfig& config) {
    fuchsia::modular::AppConfig app_config;
    app_config.url = config.url().c_str();

    if (config.has_args()) {
      app_config.args = fidl::To<fidl::VectorPtr<std::string>>(config.args());
    }

    return app_config;
  }
};
}  // namespace fidl

namespace modular {

using cobalt_registry::ModularLifetimeEventsMetricDimensionEventType;
using intl::IntlPropertyProviderImpl;

namespace {

// TODO(MF-134): This key is duplicated in
// topaz/lib/settings/lib/device_info.dart. Remove this key once factory reset
// is provided to topaz as a service.
// The key for factory reset toggles.
constexpr char kFactoryResetKey[] = "FactoryReset";

}  // namespace

BasemgrImpl::BasemgrImpl(fuchsia::modular::session::ModularConfig config,
                         const std::shared_ptr<sys::ServiceDirectory> incoming_services,
                         const std::shared_ptr<sys::OutgoingDirectory> outgoing_services,
                         fuchsia::sys::LauncherPtr launcher,
                         fuchsia::ui::policy::PresenterPtr presenter,
                         fuchsia::devicesettings::DeviceSettingsManagerPtr device_settings_manager,
                         fuchsia::wlan::service::WlanPtr wlan,
                         fuchsia::hardware::power::statecontrol::AdminPtr device_administrator,
                         fit::function<void()> on_shutdown)
    : config_(std::move(config)),
      component_context_services_(std::move(incoming_services)),
      outgoing_services_(std::move(outgoing_services)),
      launcher_(std::move(launcher)),
      presenter_(std::move(presenter)),
      device_settings_manager_(std::move(device_settings_manager)),
      wlan_(std::move(wlan)),
      device_administrator_(std::move(device_administrator)),
      on_shutdown_(std::move(on_shutdown)),
      session_provider_("SessionProvider") {
  UpdateSessionShellConfig();
  Start();
}

BasemgrImpl::~BasemgrImpl() = default;

void BasemgrImpl::Connect(
    fidl::InterfaceRequest<fuchsia::modular::internal::BasemgrDebug> request) {
  basemgr_debug_bindings_.AddBinding(this, std::move(request));
}

FuturePtr<> BasemgrImpl::StopScenic() {
  auto fut = Future<>::Create("StopScenic");
  if (!presenter_) {
    FX_LOGS(INFO) << "StopScenic: no presenter; assuming that Scenic has not been launched";
    fut->Complete();
    return fut;
  }

  // Lazily connect to lifecycle controller, instead of keeping open an often-unused channel.
  component_context_services_->Connect(scenic_lifecycle_controller_.NewRequest());
  scenic_lifecycle_controller_->Terminate();

  scenic_lifecycle_controller_.set_error_handler([fut](zx_status_t status) {
    FX_CHECK(status == ZX_ERR_PEER_CLOSED)
        << "LifecycleController experienced some error other than PEER_CLOSED : " << status
        << std::endl;
    fut->Complete();
  });
  return fut;
}

void BasemgrImpl::Start() {
  // Use the default of a random session ID unless the configuration requested persistence.
  // TODO(fxb/51752): Change base manager config to use a more direct declaration of persistence
  // and remove the base shell configuration entirely.
  if (config_.basemgr_config().base_shell().app_config().has_args()) {
    for (const auto& arg : config_.basemgr_config().base_shell().app_config().args()) {
      if (arg == "--persist_user") {
        use_random_session_id_ = false;
        break;
      }
    }
  }

  auto sessionmgr_config =
      fuchsia::modular::AppConfig{.url = modular_config::kSessionmgrUrl, .args = {}};
  auto story_shell_config =
      fidl::To<fuchsia::modular::AppConfig>(config_.basemgr_config().story_shell().app_config());
  auto intl_property_provider = IntlPropertyProviderImpl::Create(component_context_services_);
  outgoing_services_->AddPublicService(intl_property_provider->GetHandler());
  session_provider_.reset(new SessionProvider(
      /* delegate= */ this, launcher_.get(), std::move(device_administrator_),
      std::move(sessionmgr_config), CloneStruct(session_shell_config_),
      std::move(story_shell_config),
      config_.basemgr_config().use_session_shell_for_story_shell_factory(),
      std::move(intl_property_provider), CloneStruct(config_),
      /* on_zero_sessions= */
      [this] {
        if (state_ == State::SHUTTING_DOWN) {
          return;
        }
        FX_DLOGS(INFO) << "Re-starting due to session closure";
        HandleResetOrStartSession();
      }));

  ReportEvent(ModularLifetimeEventsMetricDimensionEventType::BootedToBaseMgr);
  HandleResetOrStartSession();
}

void BasemgrImpl::Shutdown() {
  FX_LOGS(INFO) << "BASEMGR SHUTDOWN";
  // Prevent the shutdown sequence from running twice.
  if (state_ == State::SHUTTING_DOWN) {
    return;
  }

  state_ = State::SHUTTING_DOWN;

  // |session_provider_| teardown is asynchronous because it holds the
  // sessionmgr processes.
  session_provider_.Teardown(kSessionProviderTimeout, [this] {
    StopScenic()->Then([this] {
      FX_DLOGS(INFO) << "- fuchsia::ui::Scenic down";
      basemgr_debug_bindings_.CloseAll(ZX_OK);
      on_shutdown_();
    });
  });
}

void BasemgrImpl::Terminate() { Shutdown(); }

void BasemgrImpl::StartSession(bool use_random_id) {
  if (state_ == State::SHUTTING_DOWN) {
    return;
  }
  if (use_random_id) {
    FX_LOGS(INFO) << "Starting session with random session ID.";
  } else {
    FX_LOGS(INFO) << "Starting session with stable session ID.";
  }

  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto did_start_session = session_provider_->StartSession(std::move(view_token), use_random_id);
  if (!did_start_session) {
    FX_LOGS(WARNING) << "New session could not be started.";
    return;
  }

  // Ownership of the Presenter should be moved to the session shell.
  if (presenter_) {
    presentation_container_ =
        std::make_unique<PresentationContainer>(presenter_.get(), std::move(view_holder_token));
    presenter_.set_error_handler([this](zx_status_t) { presentation_container_.reset(); });
  }
}

void BasemgrImpl::UpdateSessionShellConfig() {
  auto shell_count = config_.basemgr_config().session_shell_map().size();
  FX_DCHECK(shell_count > 0);

  session_shell_config_ = CloneStruct(fidl::To<fuchsia::modular::AppConfig>(
      config_.basemgr_config().session_shell_map().at(0).config().app_config()));
  if (shell_count > 1) {
    FX_LOGS(WARNING) << "More than one session shell config defined, using first in list: "
                     << session_shell_config_.url;
  }
}

void BasemgrImpl::HandleResetOrStartSession() {
  auto start_session = [this] { StartSession(use_random_session_id_); };

  // TODO(MF-347): Handle scenario where device settings manager channel is
  // dropped before error handler is set.
  // TODO(MF-134): Modular should not be handling factory reset.
  device_settings_manager_.set_error_handler(
      [start_session](zx_status_t status) { start_session(); });
  device_settings_manager_->GetInteger(
      kFactoryResetKey,
      [this, start_session](int factory_reset_value, fuchsia::devicesettings::Status status) {
        if (status == fuchsia::devicesettings::Status::ok && factory_reset_value > 0) {
          FX_LOGS(INFO) << "Factory reset initiated";
          // Unset the factory reset flag.
          device_settings_manager_->SetInteger(kFactoryResetKey, 0, [](bool result) {
            if (!result) {
              FX_LOGS(WARNING) << "Factory reset flag was not updated.";
            }
          });

          wlan_->ClearSavedNetworks(start_session);
        } else {
          start_session();
        }
      });
}

void BasemgrImpl::RestartSession(RestartSessionCallback on_restart_complete) {
  if (state_ == State::SHUTTING_DOWN) {
    return;
  }
  session_provider_->RestartSession(std::move(on_restart_complete));
}

void BasemgrImpl::StartSessionWithRandomId() { StartSession(/* use_random_id */ true); }

void BasemgrImpl::GetPresentation(
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) {
  if (!presentation_container_) {
    request.Close(ZX_ERR_NOT_FOUND);
    return;
  }
  presentation_container_->GetPresentation(std::move(request));
}

}  // namespace modular
