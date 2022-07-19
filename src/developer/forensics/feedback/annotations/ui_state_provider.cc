// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotations/ui_state_provider.h"

#include <fuchsia/ui/activity/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include <optional>
#include <variant>

#include "src/developer/forensics/feedback/annotations/constants.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/time.h"

namespace forensics::feedback {
namespace {

std::string GetUIStateString(fuchsia::ui::activity::State state) {
  switch (state) {
    case fuchsia::ui::activity::State::UNKNOWN:
      return "unknown";
    case fuchsia::ui::activity::State::IDLE:
      return "idle";
    case fuchsia::ui::activity::State::ACTIVE:
      return "active";
  }
}

}  // namespace

UIStateProvider::UIStateProvider(async_dispatcher_t* dispatcher,
                                 std::shared_ptr<sys::ServiceDirectory> services,
                                 std::unique_ptr<timekeeper::Clock> clock,
                                 std::unique_ptr<backoff::Backoff> backoff)
    : dispatcher_(dispatcher),
      services_(std::move(services)),
      clock_(std::move(clock)),
      backoff_(std::move(backoff)) {
  StartListening();
}

void UIStateProvider::StartListening() {
  provider_ptr_ = services_->Connect<fuchsia::ui::activity::Provider>();

  provider_ptr_.set_error_handler([this](zx_status_t status) {
    FX_PLOGS(WARNING, status) << "Lost connection to fuchsia.ui.activity.Provider";

    // The provider pointer and listener binding connections are not expected to close. Ensure both
    // are unbound at the same time to simplify reconnections.
    binding_.Unbind();

    OnDisconnect();
  });

  binding_.set_error_handler([this](const zx_status_t status) {
    FX_PLOGS(WARNING, status) << "Lost connection to fuchsia.ui.activity.Listener";

    // The provider pointer and listener binding connections are not expected to close. Ensure both
    // are unbound at the same time to simplify reconnections.
    provider_ptr_.Unbind();

    OnDisconnect();
  });

  provider_ptr_->WatchState(binding_.NewBinding(dispatcher_));
}

void UIStateProvider::OnDisconnect() {
  current_state_ = Error::kConnectionError;
  last_transition_time_ = Error::kConnectionError;

  if (on_update_) {
    on_update_({{kSystemUserActivityCurrentStateKey, *current_state_}});
  }

  reconnect_task_.PostDelayed(dispatcher_, backoff_->GetNext());
}

std::set<std::string> UIStateProvider::GetKeys() const {
  return {
      kSystemUserActivityCurrentStateKey,
      kSystemUserActivityCurrentDurationKey,
  };
}

void UIStateProvider::OnStateChanged(fuchsia::ui::activity::State state, int64_t transition_time,
                                     OnStateChangedCallback callback) {
  current_state_ = GetUIStateString(state);
  last_transition_time_ = zx::time(transition_time);
  callback();

  if (on_update_) {
    on_update_({{kSystemUserActivityCurrentStateKey, *current_state_}});
  }
}

Annotations UIStateProvider::Get() {
  if (std::holds_alternative<std::monostate>(last_transition_time_)) {
    return {};
  }
  if (std::holds_alternative<Error>(last_transition_time_)) {
    return {{kSystemUserActivityCurrentDurationKey, std::get<Error>(last_transition_time_)}};
  }

  const auto& time = std::get<zx::time>(last_transition_time_);
  const auto formatted_duration = FormatDuration(clock_->Now() - time);

  // FormatDuration returns std::nullopt if duration was negative- if so, send Error::kBadValue as
  // annotation value
  const auto duration =
      formatted_duration.has_value() ? ErrorOr(formatted_duration.value()) : Error::kBadValue;

  return {{kSystemUserActivityCurrentDurationKey, duration}};
}

void UIStateProvider::GetOnUpdate(::fit::function<void(Annotations)> callback) {
  on_update_ = std::move(callback);

  if (current_state_.has_value()) {
    on_update_({{kSystemUserActivityCurrentStateKey, *current_state_}});
  }
}

}  // namespace forensics::feedback
