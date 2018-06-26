// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_APP_DEVICE_SET_IMPL_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_APP_DEVICE_SET_IMPL_H_

#include <memory>

#include <fuchsia/ledger/cloud/cpp/fidl.h>

#include "lib/callback/cancellable.h"
#include "lib/fidl/cpp/array.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/cloud_provider_firebase/device_set/cloud_device_set.h"
#include "peridot/bin/cloud_provider_firebase/include/types.h"
#include "peridot/lib/firebase_auth/firebase_auth.h"

namespace cloud_provider_firebase {

// Implementation of cloud_provider::DeviceSet.
//
// If the |on_empty| callback is set, it is called when the client connection is
// closed.
class DeviceSetImpl : public cloud_provider::DeviceSet {
 public:
  DeviceSetImpl(firebase_auth::FirebaseAuth* firebase_auth,
                std::unique_ptr<CloudDeviceSet> cloud_device_set,
                fidl::InterfaceRequest<cloud_provider::DeviceSet> request);
  ~DeviceSetImpl() override;

  void set_on_empty(const fxl::Closure& on_empty) { on_empty_ = on_empty; }

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

  firebase_auth::FirebaseAuth* const firebase_auth_;
  std::unique_ptr<CloudDeviceSet> cloud_device_set_;
  fidl::Binding<cloud_provider::DeviceSet> binding_;
  fxl::Closure on_empty_;

  // Watcher set by the client.
  cloud_provider::DeviceSetWatcherPtr watcher_;
  // Keeps track of whether we already called the client callback for the most
  // recent SetWatcher() call.
  bool set_watcher_callback_called_ = false;
  // Keeps track of whether we already sent the request to update the
  // server-side timestamp storing the last time the client started watching the
  // device key.
  bool timestamp_update_request_sent_ = false;

  // Pending auth token requests to be cancelled when this class goes away.
  callback::CancellableContainer auth_token_requests_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DeviceSetImpl);
};

}  // namespace cloud_provider_firebase

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_APP_DEVICE_SET_IMPL_H_
