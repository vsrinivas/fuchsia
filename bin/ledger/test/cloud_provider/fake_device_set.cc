// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/test/cloud_provider/fake_device_set.h"

#include "peridot/bin/ledger/convert/convert.h"

namespace ledger {

FakeDeviceSet::FakeDeviceSet(CloudEraseOnCheck cloud_erase_on_check,
                             CloudEraseFromWatcher cloud_erase_from_watcher)
    : cloud_erase_on_check_(cloud_erase_on_check),
      cloud_erase_from_watcher_(cloud_erase_from_watcher) {}

FakeDeviceSet::~FakeDeviceSet() {}

void FakeDeviceSet::CheckFingerprint(fidl::Array<uint8_t> fingerprint,
                                     const CheckFingerprintCallback& callback) {
  if (cloud_erase_on_check_ == CloudEraseOnCheck::YES ||
      !fingerprints_.count(convert::ToString(fingerprint))) {
    callback(cloud_provider::Status::NOT_FOUND);
    return;
  }

  callback(cloud_provider::Status::OK);
}

void FakeDeviceSet::SetFingerprint(fidl::Array<uint8_t> fingerprint,
                                   const SetFingerprintCallback& callback) {
  fingerprints_.insert(convert::ToString(fingerprint));
  callback(cloud_provider::Status::OK);
}

void FakeDeviceSet::SetWatcher(
    fidl::Array<uint8_t> /*fingerprint*/,
    fidl::InterfaceHandle<cloud_provider::DeviceSetWatcher> watcher,
    const SetWatcherCallback& callback) {
  watcher_ = cloud_provider::DeviceSetWatcherPtr::Create(std::move(watcher));
  callback(cloud_provider::Status::OK);

  if (cloud_erase_from_watcher_ == CloudEraseFromWatcher::YES) {
    watcher_->OnCloudErased();
  }
}

void FakeDeviceSet::Erase(const EraseCallback& callback) {
  fingerprints_.clear();
  if (watcher_.is_bound()) {
    watcher_->OnCloudErased();
  }
  callback(cloud_provider::Status::OK);
}

}  // namespace ledger
