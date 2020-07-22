// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/basemgr_impl.h"

#include <fuchsia/ui/app/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <zxtest/zxtest.h>

#include "src/lib/intl/intl_property_provider_impl/intl_property_provider_impl.h"
#include "src/modular/lib/common/async_holder.h"
#include "src/modular/lib/common/teardown.h"
#include "src/modular/lib/fidl/app_client.h"
#include "src/modular/lib/fidl/clone.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"

namespace modular {

using cobalt_registry::ModularLifetimeEventsMetricDimensionEventType;
using intl::IntlPropertyProviderImpl;

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
      session_provider_("SessionProvider") {
  intl_property_provider_ = IntlPropertyProviderImpl::Create(component_context_services_);
  outgoing_services_->AddPublicService(intl_property_provider_->GetHandler());

  outgoing_services_->AddPublicService<fuchsia::modular::Lifecycle>(
      lifecycle_bindings_.GetHandler(this));

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
  auto use_random_session_id = config_accessor_.use_random_session_id();

  outgoing_services_->AddPublicService(process_lifecycle_bindings_.GetHandler(this),
                                       "fuchsia.process.lifecycle.Lifecycle");

  session_provider_.reset(new SessionProvider(
      /* delegate= */ this, launcher_.get(), std::move(device_administrator_), &config_accessor_,
      intl_property_provider_.get(),
      /* on_zero_sessions= */
      [this, use_random_session_id] {
        if (state_ == State::SHUTTING_DOWN) {
          return;
        }
        FX_DLOGS(INFO) << "Re-starting due to session closure";
        StartSession(use_random_session_id);
      }));

  ReportEvent(ModularLifetimeEventsMetricDimensionEventType::BootedToBaseMgr);
  StartSession(use_random_session_id);
}

void BasemgrImpl::Shutdown() {
  FX_LOGS(INFO) << "Shutting down basemgr";
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

void BasemgrImpl::Stop() { Shutdown(); }

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

  // TODO(fxbug.dev/56132): Ownership of the Presenter should be moved to the session shell.
  if (presenter_) {
    presentation_container_ =
        std::make_unique<PresentationContainer>(presenter_.get(), std::move(view_holder_token));
    presenter_.set_error_handler(
        [this](zx_status_t /* unused */) { presentation_container_.reset(); });
  }
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
