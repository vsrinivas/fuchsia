// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/test/cloud_provider/fake_device_set.h"

#include "peridot/bin/ledger/convert/convert.h"

namespace ledger {

FakeDeviceSet::FakeDeviceSet(
    fidl::InterfaceRequest<cloud_provider::DeviceSet> request)
    : binding_(this, std::move(request)) {
  // The class shuts down when the client connection is disconnected.
  binding_.set_connection_error_handler([this] {
    if (on_empty_) {
      on_empty_();
    }
  });
}

FakeDeviceSet::~FakeDeviceSet() {}

void FakeDeviceSet::CheckFingerprint(fidl::Array<uint8_t> fingerprint,
                                     const CheckFingerprintCallback& callback) {
  if (!fingerprints_.count(convert::ToString(fingerprint))) {
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
    fidl::Array<uint8_t> fingerprint,
    fidl::InterfaceHandle<cloud_provider::DeviceSetWatcher> watcher,
    const SetWatcherCallback& callback) {
  watcher_ = cloud_provider::DeviceSetWatcherPtr::Create(std::move(watcher));
  callback(cloud_provider::Status::OK);
}

void FakeDeviceSet::Erase(const EraseCallback& callback) {
  fingerprints_.clear();
  if (watcher_.is_bound()) {
    watcher_->OnCloudErased();
  }
  callback(cloud_provider::Status::OK);
}

}  // namespace ledger
