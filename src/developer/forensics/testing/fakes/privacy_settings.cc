// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/testing/fakes/privacy_settings.h"

#include <lib/fpromise/result.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

namespace forensics {
namespace fakes {

void PrivacySettings::CloseConnection() {
  if (binding_) {
    binding_->Close(ZX_ERR_PEER_CLOSED);
  }
}

void PrivacySettings::Watch(WatchCallback callback) {
  FX_CHECK(!watcher_);
  watcher_ = std::make_unique<WatchCallback>(std::move(callback));
  if (dirty_bit_) {
    NotifyWatcher();
  }
}

void PrivacySettings::Set(fuchsia::settings::PrivacySettings settings, SetCallback callback) {
  settings_ = std::move(settings);
  callback(::fpromise::ok());
  dirty_bit_ = true;

  if (watcher_) {
    NotifyWatcher();
  }
}

void PrivacySettings::NotifyWatcher() {
  fuchsia::settings::PrivacySettings settings;
  settings_.Clone(&settings);
  (*watcher_)(std::move(settings));
  watcher_.reset();
  dirty_bit_ = false;
}

void PrivacySettingsClosesConnectionOnFirstWatch::Watch(WatchCallback callback) {
  if (first_watch_) {
    CloseConnection();
    first_watch_ = false;
    return;
  }

  FX_CHECK(!watcher_);
  watcher_ = std::make_unique<WatchCallback>(std::move(callback));
  if (dirty_bit_) {
    NotifyWatcher();
  }
}

}  // namespace fakes
}  // namespace forensics
