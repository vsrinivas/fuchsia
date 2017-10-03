// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TEST_CLOUD_PROVIDER_FAKE_DEVICE_SET_H_
#define PERIDOT_BIN_LEDGER_TEST_CLOUD_PROVIDER_FAKE_DEVICE_SET_H_

#include <set>
#include <string>

#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"

namespace ledger {

class FakeDeviceSet : public cloud_provider::DeviceSet {
 public:
  FakeDeviceSet(fidl::InterfaceRequest<cloud_provider::DeviceSet> request);
  ~FakeDeviceSet() override;

  void set_on_empty(const fxl::Closure& on_empty) { on_empty_ = on_empty; }

 private:
  void CheckFingerprint(fidl::Array<uint8_t> fingerprint,
                        const CheckFingerprintCallback& callback) override;

  void SetFingerprint(fidl::Array<uint8_t> fingerprint,
                      const SetFingerprintCallback& callback) override;

  void SetWatcher(
      fidl::Array<uint8_t> fingerprint,
      fidl::InterfaceHandle<cloud_provider::DeviceSetWatcher> watcher,
      const SetWatcherCallback& callback) override;

  void Erase(const EraseCallback& callback) override;

  fidl::Binding<cloud_provider::DeviceSet> binding_;
  fxl::Closure on_empty_;

  std::set<std::string> fingerprints_;

  // Watcher set by the client.
  cloud_provider::DeviceSetWatcherPtr watcher_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeDeviceSet);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_TEST_CLOUD_PROVIDER_FAKE_DEVICE_SET_H_
