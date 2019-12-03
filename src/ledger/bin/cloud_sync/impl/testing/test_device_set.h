// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_TESTING_TEST_DEVICE_SET_H_
#define SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_TESTING_TEST_DEVICE_SET_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>

#include "src/ledger/bin/fidl/include/types.h"

namespace cloud_sync {

class TestDeviceSet : public cloud_provider::DeviceSet {
 public:
  TestDeviceSet();
  TestDeviceSet(const TestDeviceSet&) = delete;
  TestDeviceSet& operator=(const TestDeviceSet&) = delete;
  ~TestDeviceSet() override;

  cloud_provider::Status status_to_return = cloud_provider::Status::OK;
  cloud_provider::Status set_watcher_status_to_return = cloud_provider::Status::OK;
  std::string checked_fingerprint;
  std::string set_fingerprint;

  int set_watcher_calls = 0;
  std::string watched_fingerprint;
  cloud_provider::DeviceSetWatcherPtr set_watcher;

 private:
  // cloud_provider::DeviceSet:
  void CheckFingerprint(std::vector<uint8_t> fingerprint,
                        CheckFingerprintCallback callback) override;

  void SetFingerprint(std::vector<uint8_t> fingerprint, SetFingerprintCallback callback) override;

  void SetWatcher(std::vector<uint8_t> fingerprint,
                  fidl::InterfaceHandle<cloud_provider::DeviceSetWatcher> watcher,
                  SetWatcherCallback callback) override;

  void Erase(EraseCallback callback) override;
};

}  // namespace cloud_sync

#endif  // SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_TESTING_TEST_DEVICE_SET_H_
