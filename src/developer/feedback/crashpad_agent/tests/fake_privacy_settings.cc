// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/tests/fake_privacy_settings.h"

#include <lib/fit/result.h>

#include <memory>

#include "src/lib/fxl/logging.h"

namespace feedback {

void FakePrivacySettings::CloseConnection() {
  if (binding_) {
    binding_->Close(ZX_ERR_PEER_CLOSED);
  }
}

void FakePrivacySettings::Watch(WatchCallback callback) {
  FXL_CHECK(!watcher_);
  watcher_ = std::make_unique<WatchCallback>(std::move(callback));
  if (dirty_bit_) {
    NotifyWatcher();
  }
}

void FakePrivacySettings::Set(fuchsia::settings::PrivacySettings settings, SetCallback callback) {
  settings_ = std::move(settings);
  callback(fit::ok());
  dirty_bit_ = true;

  if (watcher_) {
    NotifyWatcher();
  }
}

void FakePrivacySettings::NotifyWatcher() {
  fuchsia::settings::PrivacySettings settings;
  settings_.Clone(&settings);
  (*watcher_)(fit::ok(std::move(settings)));
  watcher_.reset();
  dirty_bit_ = false;
}

}  // namespace feedback
