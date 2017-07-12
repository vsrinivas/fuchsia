// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_SYNC_PUBLIC_LOCAL_VERSION_CHECKER_H_
#define APPS_LEDGER_SRC_CLOUD_SYNC_PUBLIC_LOCAL_VERSION_CHECKER_H_

#include <functional>
#include <string>

#include "apps/ledger/src/firebase/firebase.h"

namespace cloud_sync {

// Detects cloud state being erased since the last time the device synced.
// TODO(ppi): rename this to CloudDeviceSet or something similar.
//
// The class uses a list of device fingerprints kept in the cloud alongside with
// page data. Every device of a user keeps a random persisted fingerprint
// locally on disk and in the cloud. When the cloud is wiped, all of the
// fingerprints are removed, allowing each device to recognize that the cloud
// was erased.
class LocalVersionChecker {
 public:
  enum class Status {
    // Cloud state is compatible, ie. the fingerprint of the device is still in
    // the list.
    OK,
    // Cloud state is not compatible, ie. it was erased without erasing the
    // local state on this device.
    ERASED,
    // Couldn't determine the compatibility due to a network error.
    NETWORK_ERROR
  };

  LocalVersionChecker(){};
  virtual ~LocalVersionChecker(){};

  // Verifies that the device fingerprint in the cloud is still in the list of
  // devices, ensuring that the cloud was not erased since the last sync.
  // This makes at most one network request using the given |auth_token|.
  virtual void CheckFingerprint(std::string auth_token,
                                std::string fingerprint,
                                std::function<void(Status)> callback) = 0;

  // Adds the device fingerprint to the list of devices in the cloud.
  // This makes at most one network request using the given |auth_token|.
  virtual void SetFingerprint(std::string auth_token,
                              std::string fingerprint,
                              std::function<void(Status)> callback) = 0;

  // Watches the fingerprint in the cloud. The given |callback| is called with
  // status OK when the watcher is correctly set. Upon an error it is called
  // again with a non-OK status. After the |callback| is called with a non-OK
  // status, it is never called again.
  //
  // This makes at most one network request using the given |auth_token|.
  virtual void WatchFingerprint(std::string auth_token,
                                std::string fingerprint,
                                std::function<void(Status)> callback) = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(LocalVersionChecker);
};

}  // namespace cloud_sync

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_PUBLIC_LOCAL_VERSION_CHECKER_H_
