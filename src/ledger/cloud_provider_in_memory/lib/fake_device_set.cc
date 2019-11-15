// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/cloud_provider_in_memory/lib/fake_device_set.h"

#include "peridot/lib/convert/convert.h"

namespace ledger {

FakeDeviceSet::FakeDeviceSet(CloudEraseOnCheck cloud_erase_on_check,
                             CloudEraseFromWatcher cloud_erase_from_watcher,
                             fit::closure on_watcher_set)
    : cloud_erase_on_check_(cloud_erase_on_check),
      cloud_erase_from_watcher_(cloud_erase_from_watcher),
      on_watcher_set_(std::move(on_watcher_set)) {}

FakeDeviceSet::~FakeDeviceSet() = default;

void FakeDeviceSet::CheckFingerprint(std::vector<uint8_t> fingerprint,
                                     CheckFingerprintCallback callback) {
  if (cloud_erase_on_check_ == CloudEraseOnCheck::YES ||
      !fingerprints_.count(convert::ToString(fingerprint))) {
    callback(cloud_provider::Status::NOT_FOUND);
    return;
  }

  callback(cloud_provider::Status::OK);
}

void FakeDeviceSet::SetFingerprint(std::vector<uint8_t> fingerprint,
                                   SetFingerprintCallback callback) {
  fingerprints_.insert(convert::ToString(fingerprint));
  callback(cloud_provider::Status::OK);
}

void FakeDeviceSet::SetWatcher(std::vector<uint8_t> fingerprint,
                               fidl::InterfaceHandle<cloud_provider::DeviceSetWatcher> watcher,
                               SetWatcherCallback callback) {
  // TODO(ppi): for the cloud provider to be useful for Voila, we need
  // to support multiple watchers.
  if (fingerprints_.count(convert::ToString(fingerprint)) == 0) {
    callback(cloud_provider::Status::NOT_FOUND);
    return;
  }
  watcher_ = watcher.Bind();
  callback(cloud_provider::Status::OK);
  if (on_watcher_set_) {
    on_watcher_set_();
  }

  if (cloud_erase_from_watcher_ == CloudEraseFromWatcher::YES) {
    watcher_->OnCloudErased();
  }
}

void FakeDeviceSet::Erase(EraseCallback callback) {
  fingerprints_.clear();
  if (watcher_.is_bound()) {
    watcher_->OnCloudErased();
  }
  callback(cloud_provider::Status::OK);
}

}  // namespace ledger
