// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotations/ui_state_provider.h"

#include <fuchsia/ui/activity/cpp/fidl.h>

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

UIStateProvider::UIStateProvider(std::unique_ptr<timekeeper::Clock> clock)
    : clock_(std::move(clock)) {}

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
  // Do not return values if OnStateChanged hasn't been called yet
  if (!last_transition_time_.has_value()) {
    return {};
  }

  const auto formatted_duration = FormatDuration(clock_->Now() - last_transition_time_.value());

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
