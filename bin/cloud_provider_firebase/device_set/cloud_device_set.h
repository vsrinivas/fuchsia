// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_DEVICE_SET_CLOUD_DEVICE_SET_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_DEVICE_SET_CLOUD_DEVICE_SET_H_

#include <functional>
#include <string>

#include "peridot/lib/firebase/firebase.h"

namespace cloud_provider_firebase {

// Keeps track of the different devices syncing through the cloud by maintaining
// a set of device fingerprints in the cloud.
//
// Every device of a user keeps a random persisted fingerprint locally on disk
// and in the cloud. When the cloud is wiped, all of the fingerprints are
// removed, allowing each device to recognize that the cloud was erased.
class CloudDeviceSet {
 public:
  enum class Status {
    // Cloud state is compatible, ie. the fingerprint of the device is still in
    // the list.
    OK,
    // Cloud state is not compatible, ie. it was erased without erasing the
    // local state on this device.
    ERASED,
    // Couldn't determine the compatibility due to a network error.
    // Causes of this error include lack of connectivity and using an expired
    // authentication token.
    NETWORK_ERROR
  };

  CloudDeviceSet(){};
  virtual ~CloudDeviceSet(){};

  // Verifies that the device fingerprint in the cloud is still in the list of
  // devices, ensuring that the cloud was not erased since the last sync.
  // This makes at most one network request using the given |auth_token|.
  virtual void CheckFingerprint(std::string auth_token, std::string fingerprint,
                                std::function<void(Status)> callback) = 0;

  // Adds the device fingerprint to the list of devices in the cloud.
  // This makes at most one network request using the given |auth_token|.
  virtual void SetFingerprint(std::string auth_token, std::string fingerprint,
                              std::function<void(Status)> callback) = 0;

  // Watches the fingerprint in the cloud. The given |callback| is called with
  // status OK when the watcher is correctly set. Upon an error it is called
  // again with a non-OK status. After the |callback| is called with a non-OK
  // status, it is never called again.
  //
  // This makes at most one network request using the given |auth_token|.
  virtual void WatchFingerprint(std::string auth_token, std::string fingerprint,
                                std::function<void(Status)> callback) = 0;

  // Erases the entire device set. The resulting status is OK if the erase
  // succeeded and NETWORK_ERROR if not.
  virtual void EraseAllFingerprints(std::string auth_token,
                                    std::function<void(Status)> callback) = 0;

  // Sends a request to update the server-side timestamp associated with the
  // |fingerprint|.
  virtual void UpdateTimestampAssociatedWithFingerprint(
      std::string auth_token, std::string fingerprint) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(CloudDeviceSet);
};

}  // namespace cloud_provider_firebase

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_DEVICE_SET_CLOUD_DEVICE_SET_H_
