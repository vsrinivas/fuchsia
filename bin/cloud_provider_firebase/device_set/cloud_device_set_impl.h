// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_DEVICE_SET_CLOUD_DEVICE_SET_IMPL_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_DEVICE_SET_CLOUD_DEVICE_SET_IMPL_H_

#include <functional>
#include <string>

#include <rapidjson/document.h>

#include "lib/callback/destruction_sentinel.h"
#include "peridot/bin/cloud_provider_firebase/device_set/cloud_device_set.h"
#include "peridot/lib/firebase/firebase.h"
#include "peridot/lib/firebase/watch_client.h"

namespace cloud_provider_firebase {

// Path under which the device map is stored in the cloud, relative to the root
// of the user storage.
constexpr char kDeviceMapRelpath[] = "__metadata/devices";

class CloudDeviceSetImpl : public CloudDeviceSet, public firebase::WatchClient {
 public:
  explicit CloudDeviceSetImpl(
      std::unique_ptr<firebase::Firebase> user_firebase);
  ~CloudDeviceSetImpl() override;

  void CheckFingerprint(std::string auth_token, std::string fingerprint,
                        std::function<void(Status)> callback) override;

  void SetFingerprint(std::string auth_token, std::string fingerprint,
                      std::function<void(Status)> callback) override;

  void WatchFingerprint(std::string auth_token, std::string fingerprint,
                        std::function<void(Status)> callback) override;

  void EraseAllFingerprints(std::string auth_token,
                            std::function<void(Status)> callback) override;

  void UpdateTimestampAssociatedWithFingerprint(
      std::string auth_token, std::string fingerprint) override;

  // firebase::WatchClient:
  void OnPut(const std::string& path, const rapidjson::Value& value) override;
  void OnPatch(const std::string& path, const rapidjson::Value& value) override;
  void OnCancel() override;
  void OnAuthRevoked(const std::string& reason) override;
  void OnMalformedEvent() override;
  void OnConnectionError() override;

 private:
  void ResetWatcher();
  std::unique_ptr<firebase::Firebase> user_firebase_;
  bool firebase_watcher_set_ = false;
  std::function<void(Status)> watch_callback_;

  callback::DestructionSentinel destruction_sentinel_;
};

}  // namespace cloud_provider_firebase

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_DEVICE_SET_CLOUD_DEVICE_SET_IMPL_H_
