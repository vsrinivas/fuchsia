// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/device_set/test/test_cloud_device_set.h"

namespace cloud_provider_firebase {

TestCloudDeviceSet::TestCloudDeviceSet(fxl::RefPtr<fxl::TaskRunner> task_runner)
    : task_runner_(std::move(task_runner)) {}

TestCloudDeviceSet::~TestCloudDeviceSet() {}

void TestCloudDeviceSet::CheckFingerprint(
    std::string /*auth_token*/,
    std::string fingerprint,
    std::function<void(Status)> callback) {
  checked_fingerprint = fingerprint;
  task_runner_->PostTask(
      [status = status_to_return, callback = std::move(callback)] {
        callback(status);
      });
}

void TestCloudDeviceSet::SetFingerprint(std::string /*auth_token*/,
                                        std::string fingerprint,
                                        std::function<void(Status)> callback) {
  set_fingerprint = fingerprint;
  task_runner_->PostTask(
      [status = status_to_return, callback = std::move(callback)] {
        callback(status);
      });
}

void TestCloudDeviceSet::WatchFingerprint(
    std::string /*auth_token*/,
    std::string fingerprint,
    std::function<void(Status)> callback) {
  watched_fingerprint = fingerprint;
  watch_callback = callback;
}

void TestCloudDeviceSet::EraseAllFingerprints(
    std::string auth_token,
    std::function<void(Status)> callback) {
  task_runner_->PostTask(
      [status = status_to_return, callback = std::move(callback)] {
        callback(status);
      });
}

}  // namespace cloud_provider_firebase
