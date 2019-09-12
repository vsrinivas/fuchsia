// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/tests/fake_privacy_settings.h"

#include <memory>

#include "fuchsia/settings/cpp/fidl.h"

namespace feedback {

void FakePrivacySettings::CloseConnection() {
  if (binding_) {
    binding_->Unbind();
  }
}

void FakePrivacySettings::Watch(WatchCallback callback) {
  if (!first_call_) {
    watchers_.push_back(std::move(callback));
    return;
  }

  fuchsia::settings::Privacy_Watch_Response response;
  settings_.Clone(&response.settings);
  fuchsia::settings::Privacy_Watch_Result result;
  result.set_response(std::move(response));
  callback(std::move(result));
  first_call_ = false;
}

void FakePrivacySettings::Set(fuchsia::settings::PrivacySettings settings, SetCallback callback) {
  settings_ = std::move(settings);
  fuchsia::settings::Privacy_Set_Result result;
  result.set_response({});
  callback(std::move(result));

  NotifyWatchers();
}

void FakePrivacySettings::NotifyWatchers() {
  for (const auto& watcher : watchers_) {
    fuchsia::settings::Privacy_Watch_Response response;
    settings_.Clone(&response.settings);
    fuchsia::settings::Privacy_Watch_Result result;
    result.set_response(std::move(response));
    watcher(std::move(result));
  }
  watchers_.clear();
}

}  // namespace feedback
