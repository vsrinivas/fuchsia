// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_DEVICE_SET_TEST_TEST_CLOUD_DEVICE_SET_H_
#define APPS_LEDGER_SRC_DEVICE_SET_TEST_TEST_CLOUD_DEVICE_SET_H_

#include <functional>
#include <string>

#include "peridot/bin/ledger/device_set/cloud_device_set.h"
#include "lib/fxl/tasks/task_runner.h"

namespace cloud_provider_firebase {

class TestCloudDeviceSet : public cloud_provider_firebase::CloudDeviceSet {
 public:
  explicit TestCloudDeviceSet(fxl::RefPtr<fxl::TaskRunner> task_runner);

  ~TestCloudDeviceSet() override;

  void CheckFingerprint(std::string auth_token,
                        std::string fingerprint,
                        std::function<void(Status)> callback) override;

  void SetFingerprint(std::string auth_token,
                      std::string fingerprint,
                      std::function<void(Status)> callback) override;

  void WatchFingerprint(std::string auth_token,
                        std::string fingerprint,
                        std::function<void(Status)> callback) override;

  CloudDeviceSet::Status status_to_return = CloudDeviceSet::Status::OK;

  std::string checked_fingerprint;
  std::string set_fingerprint;
  std::string watched_fingerprint;
  std::function<void(Status)> watch_callback;

 private:
  fxl::RefPtr<fxl::TaskRunner> task_runner_;
};

}  // namespace cloud_provider_firebase

#endif  // APPS_LEDGER_SRC_DEVICE_SET_TEST_TEST_CLOUD_DEVICE_SET_H_
