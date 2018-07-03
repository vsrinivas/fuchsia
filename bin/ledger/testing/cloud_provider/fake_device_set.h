// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTING_CLOUD_PROVIDER_FAKE_DEVICE_SET_H_
#define PERIDOT_BIN_LEDGER_TESTING_CLOUD_PROVIDER_FAKE_DEVICE_SET_H_

#include <set>
#include <string>

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/fidl/cpp/array.h>
#include <lib/fit/function.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/testing/cloud_provider/types.h"

namespace ledger {

class FakeDeviceSet : public cloud_provider::DeviceSet {
 public:
  FakeDeviceSet(CloudEraseOnCheck cloud_erase_on_check,
                CloudEraseFromWatcher cloud_erase_from_watcher);
  ~FakeDeviceSet() override;

  void set_on_empty(fit::closure on_empty) { on_empty_ = std::move(on_empty); }

 private:
  void CheckFingerprint(fidl::VectorPtr<uint8_t> fingerprint,
                        CheckFingerprintCallback callback) override;

  void SetFingerprint(fidl::VectorPtr<uint8_t> fingerprint,
                      SetFingerprintCallback callback) override;

  void SetWatcher(
      fidl::VectorPtr<uint8_t> fingerprint,
      fidl::InterfaceHandle<cloud_provider::DeviceSetWatcher> watcher,
      SetWatcherCallback callback) override;

  void Erase(EraseCallback callback) override;

  const CloudEraseOnCheck cloud_erase_on_check_ = CloudEraseOnCheck::NO;

  const CloudEraseFromWatcher cloud_erase_from_watcher_ =
      CloudEraseFromWatcher::NO;

  fit::closure on_empty_;

  std::set<std::string> fingerprints_;

  // Watcher set by the client.
  cloud_provider::DeviceSetWatcherPtr watcher_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeDeviceSet);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_TESTING_CLOUD_PROVIDER_FAKE_DEVICE_SET_H_
