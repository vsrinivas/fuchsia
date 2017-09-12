// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_CLOUD_PROVIDER_FIREBASE_DEVICE_SET_IMPL_H_
#define APPS_LEDGER_CLOUD_PROVIDER_FIREBASE_DEVICE_SET_IMPL_H_

#include "apps/ledger/services/cloud_provider/cloud_provider.fidl.h"
#include "apps/ledger/src/auth_provider/auth_provider.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"

namespace cloud_provider_firebase {

// Implementation of cloud_provider::DeviceSet.
//
// If the |on_empty| callback is set, it is called when the client connection is
// closed.
class DeviceSetImpl : public cloud_provider::DeviceSet {
 public:
  DeviceSetImpl(auth_provider::AuthProvider* auth_provider,
                fidl::InterfaceRequest<cloud_provider::DeviceSet> request);
  ~DeviceSetImpl() override;

  void set_on_empty(const fxl::Closure& on_empty) { on_empty_ = on_empty; }

 private:
  auth_provider::AuthProvider* const auth_provider_;
  fidl::Binding<cloud_provider::DeviceSet> binding_;
  fxl::Closure on_empty_;

  void CheckFingerprint(fidl::Array<uint8_t> fingerprint,
                        const CheckFingerprintCallback& callback) override;

  void SetFingerprint(fidl::Array<uint8_t> fingerprint,
                      const SetFingerprintCallback& callback) override;

  void SetWatcher(
      fidl::InterfaceHandle<cloud_provider::DeviceSetWatcher> watcher,
      fidl::Array<uint8_t> fingerprint,
      const SetWatcherCallback& callback) override;

  FXL_DISALLOW_COPY_AND_ASSIGN(DeviceSetImpl);
};

}  // namespace cloud_provider_firebase

#endif  // APPS_LEDGER_CLOUD_PROVIDER_FIREBASE_DEVICE_SET_IMPL_H_
