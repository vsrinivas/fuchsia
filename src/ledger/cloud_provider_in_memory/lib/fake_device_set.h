// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_CLOUD_PROVIDER_IN_MEMORY_LIB_FAKE_DEVICE_SET_H_
#define SRC_LEDGER_CLOUD_PROVIDER_IN_MEMORY_LIB_FAKE_DEVICE_SET_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/fit/function.h>

#include <set>
#include <string>

#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/cloud_provider_in_memory/lib/types.h"
#include "src/lib/fxl/macros.h"

namespace ledger {

class FakeDeviceSet : public cloud_provider::DeviceSet {
 public:
  FakeDeviceSet(CloudEraseOnCheck cloud_erase_on_check,
                CloudEraseFromWatcher cloud_erase_from_watcher, fit::closure on_watcher_set);
  ~FakeDeviceSet() override;

 private:
  void CheckFingerprint(std::vector<uint8_t> fingerprint,
                        CheckFingerprintCallback callback) override;

  void SetFingerprint(std::vector<uint8_t> fingerprint, SetFingerprintCallback callback) override;

  void SetWatcher(std::vector<uint8_t> fingerprint,
                  fidl::InterfaceHandle<cloud_provider::DeviceSetWatcher> watcher,
                  SetWatcherCallback callback) override;

  void Erase(EraseCallback callback) override;

  const CloudEraseOnCheck cloud_erase_on_check_ = CloudEraseOnCheck::NO;

  const CloudEraseFromWatcher cloud_erase_from_watcher_ = CloudEraseFromWatcher::NO;

  fit::closure on_watcher_set_;

  std::set<std::string> fingerprints_;

  // Watcher set by the client.
  cloud_provider::DeviceSetWatcherPtr watcher_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeDeviceSet);
};

}  // namespace ledger

#endif  // SRC_LEDGER_CLOUD_PROVIDER_IN_MEMORY_LIB_FAKE_DEVICE_SET_H_
