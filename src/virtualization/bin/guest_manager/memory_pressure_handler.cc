// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/guest_manager/memory_pressure_handler.h"

#include <fidl/fuchsia.memorypressure/cpp/markers.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/assert.h>

#include <cstdint>

#include <virtio/balloon.h>
#include <virtio/virtio_ids.h>

MemoryPressureHandler::MemoryPressureHandler(async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher), last_inflate_time_(zx::time::infinite()) {}

void MemoryPressureHandler::UpdateTargetBalloonSize() {
  if (target_balloon_state_ == TargetBalloonState::Inflated) {
    // Calculate the target balloon size as
    // kBalloonAvailableMemoryInflatePercentage of all available memory.
    // Some of the memory might be already inside of the balloon, so we'll take
    // current balloon size into account
    balloon_controller_->GetMemStats().Then(
        [this](fidl::Result<::fuchsia_virtualization::BalloonController::GetMemStats>& result) {
          if (result.is_error()) {
            FX_LOGS(ERROR) << "Failed GetMemStats: " << result.error_value();
            return;
          }
          for (auto& val : result->mem_stats().value()) {
            if (val.tag() == VIRTIO_BALLOON_S_AVAIL) {
              const uint32_t avail_memory_pages =
                  static_cast<uint32_t>(val.val() / static_cast<uint64_t>(PAGE_SIZE));
              balloon_controller_->GetBalloonSize().Then(
                  [avail_memory_pages,
                   this](fidl::Result<::fuchsia_virtualization::BalloonController::GetBalloonSize>&
                             result) {
                    if (result.is_error()) {
                      FX_LOGS(ERROR) << "Failed GetBalloonSize: " << result.error_value();
                      return;
                    }
                    uint32_t new_target = (avail_memory_pages + result->current_num_pages()) / 100 *
                                          kBalloonAvailableMemoryInflatePercentage;
                    last_inflate_time_ = async::Now(dispatcher_);
                    auto res = balloon_controller_->RequestNumPages(new_target);
                    if (res.is_error()) {
                      FX_LOGS(ERROR) << "Failed RequestNumPages: " << res.error_value();
                      return;
                    }
                  });
              break;
            }
          }
        });
  } else {
    auto res = balloon_controller_->RequestNumPages(0);
    if (res.is_error()) {
      FX_LOGS(ERROR) << "Failed RequestNumPages: " << res.error_value();
      return;
    }
  }
}

void MemoryPressureHandler::OnLevelChanged(OnLevelChangedRequest& request,
                                           OnLevelChangedCompleter::Sync& completer) {
  TargetBalloonState new_balloon_state =
      (request.level() == fuchsia_memorypressure::Level::kWarning ||
       request.level() == fuchsia_memorypressure::Level::kCritical)
          ? TargetBalloonState::Inflated
          : TargetBalloonState::Deflated;

  if (target_balloon_state_ == new_balloon_state) {
    completer.Reply();
    return;
  }

  target_balloon_state_ = new_balloon_state;
  // We only need to update the target balloon state if balloon update has been scheduled already
  if (!delayed_task_scheduled_) {
    // Check if we need to delay balloon update
    zx::time deadline;
    if (last_inflate_time_ != zx::time::infinite()) {
      if (new_balloon_state == TargetBalloonState::Inflated) {
        deadline = last_inflate_time_ + kBalloonRepeatedInflateWaitTime;
      } else {
        deadline = last_inflate_time_ + kBalloonInflateCompletionWaitTime;
      }
    }
    delayed_task_scheduled_ = true;
    async::PostTaskForTime(
        dispatcher_,
        [this]() mutable {
          ZX_ASSERT(delayed_task_scheduled_);
          UpdateTargetBalloonSize();
          delayed_task_scheduled_ = false;
        },
        deadline);
  }
  completer.Reply();
}

zx_status_t MemoryPressureHandler::Start(sys::ComponentContext* context) {
  auto dir = fidl::ClientEnd<fuchsia_io::Directory>(context->svc()->CloneChannel().TakeChannel());
  // Set ourselves to receive memory pressure notifications
  zx::result mempressure_client_end =
      component::ConnectAt<fuchsia_memorypressure::Provider>(dir.borrow());
  if (mempressure_client_end.is_error()) {
    FX_LOGS(ERROR) << "Failed to connect to the memory pressure Provider"
                   << mempressure_client_end.status_string();
    return mempressure_client_end.error_value();
  }
  fidl::Client memory_provider(std::move(*mempressure_client_end), dispatcher_, this);

  auto mempressure_endpoints = fidl::CreateEndpoints<fuchsia_memorypressure::Watcher>();
  memory_pressure_server_ = fidl::BindServer<fidl::Server<fuchsia_memorypressure::Watcher>>(
      dispatcher_, std::move(mempressure_endpoints->server), this);
  auto res = memory_provider->RegisterWatcher(std::move(mempressure_endpoints->client));
  if (res.is_error()) {
    FX_LOGS(ERROR) << "Failed to register memory pressure watcher. Error=" << res.error_value();
  }
  // Connecting to the balloon controller
  zx::result guest_client_end = component::ConnectAt<fuchsia_virtualization::Guest>(dir.borrow());
  if (guest_client_end.is_error()) {
    FX_LOGS(ERROR) << "Failed to connect to the Guest" << guest_client_end.status_string();
    return guest_client_end.error_value();
  }
  fidl::Client guest(std::move(*guest_client_end), dispatcher_);

  auto balloon_controller_endpoints =
      fidl::CreateEndpoints<fuchsia_virtualization::BalloonController>();
  balloon_controller_.Bind(std::move(balloon_controller_endpoints->client), dispatcher_, this);

  guest->GetBalloonController(std::move(balloon_controller_endpoints->server))
      .Then([](fidl::Result<::fuchsia_virtualization::Guest::GetBalloonController>& res) {
        if (res.is_error()) {
          FX_LOGS(WARNING) << "Failed GetBalloonController: " << res.error_value();
        }
      });
  return ZX_OK;
}

void MemoryPressureHandler::on_fidl_error(fidl::UnbindInfo error) {
  memory_pressure_server_->Unbind();
}
