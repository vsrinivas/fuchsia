// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/tests/fake_privacy_settings.h"

#include <lib/fit/result.h>

#include <memory>

#include "fuchsia/settings/cpp/fidl.h"

namespace feedback {

void FakePrivacySettings::CloseConnection() {
  if (binding_) {
    binding_->Close(ZX_ERR_PEER_CLOSED);
  }
}

void FakePrivacySettings::Watch(WatchCallback callback) {
  if (!first_call_) {
    watchers_.push_back(std::move(callback));
    return;
  }

  fuchsia::settings::PrivacySettings settings;
  settings_.Clone(&settings);
  callback(fit::ok(std::move(settings)));
  first_call_ = false;
}

void FakePrivacySettings::Set(fuchsia::settings::PrivacySettings settings, SetCallback callback) {
  settings_ = std::move(settings);
  callback(fit::ok());

  NotifyWatchers();
}

void FakePrivacySettings::NotifyWatchers() {
  for (const auto& watcher : watchers_) {
    fuchsia::settings::PrivacySettings settings;
    settings_.Clone(&settings);
    watcher(fit::ok(std::move(settings)));
  }
  watchers_.clear();
}

}  // namespace feedback
