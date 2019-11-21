// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cloud_sync/impl/testing/test_device_set.h"

#include "src/ledger/lib/convert/convert.h"

namespace cloud_sync {

TestDeviceSet::TestDeviceSet() = default;
TestDeviceSet::~TestDeviceSet() = default;

void TestDeviceSet::CheckFingerprint(std::vector<uint8_t> fingerprint,
                                     CheckFingerprintCallback callback) {
  checked_fingerprint = convert::ToString(fingerprint);
  callback(status_to_return);
}

void TestDeviceSet::SetFingerprint(std::vector<uint8_t> fingerprint,
                                   SetFingerprintCallback callback) {
  set_fingerprint = convert::ToString(fingerprint);
  callback(status_to_return);
}

void TestDeviceSet::SetWatcher(std::vector<uint8_t> fingerprint,
                               fidl::InterfaceHandle<cloud_provider::DeviceSetWatcher> watcher,
                               SetWatcherCallback callback) {
  set_watcher_calls++;
  watched_fingerprint = convert::ToString(fingerprint);
  set_watcher = watcher.Bind();
  if (set_watcher_status_to_return != cloud_provider::Status::OK) {
    set_watcher->OnError(set_watcher_status_to_return);
  }
  callback(set_watcher_status_to_return);
}

void TestDeviceSet::Erase(EraseCallback callback) { callback(status_to_return); }

}  // namespace cloud_sync
