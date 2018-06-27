// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/device_set/testing/test_cloud_device_set.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>

namespace cloud_provider_firebase {

TestCloudDeviceSet::TestCloudDeviceSet(async_t* async) : async_(async) {}

TestCloudDeviceSet::~TestCloudDeviceSet() {}

void TestCloudDeviceSet::CheckFingerprint(
    std::string /*auth_token*/, std::string fingerprint,
    fit::function<void(Status)> callback) {
  checked_fingerprint = fingerprint;
  async::PostTask(async_,
                  [status = status_to_return, callback = std::move(callback)] {
                    callback(status);
                  });
}

void TestCloudDeviceSet::SetFingerprint(std::string /*auth_token*/,
                                        std::string fingerprint,
                                        fit::function<void(Status)> callback) {
  set_fingerprint = fingerprint;
  async::PostTask(async_,
                  [status = status_to_return, callback = std::move(callback)] {
                    callback(status);
                  });
}

void TestCloudDeviceSet::WatchFingerprint(
    std::string /*auth_token*/, std::string fingerprint,
    fit::function<void(Status)> callback) {
  watched_fingerprint = fingerprint;
  watch_callback = std::move(callback);
}

void TestCloudDeviceSet::EraseAllFingerprints(
    std::string auth_token, fit::function<void(Status)> callback) {
  async::PostTask(async_,
                  [status = status_to_return, callback = std::move(callback)] {
                    callback(status);
                  });
}

void TestCloudDeviceSet::UpdateTimestampAssociatedWithFingerprint(
    std::string auth_token, std::string fingerprint) {
  timestamp_update_requests_++;
}

}  // namespace cloud_provider_firebase
