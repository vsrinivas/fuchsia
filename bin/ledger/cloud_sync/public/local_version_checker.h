// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_SYNC_PUBLIC_LOCAL_VERSION_CHECKER_H_
#define APPS_LEDGER_SRC_CLOUD_SYNC_PUBLIC_LOCAL_VERSION_CHECKER_H_

#include <functional>
#include <string>

#include "apps/ledger/src/firebase/firebase.h"

namespace cloud_sync {

class LocalVersionChecker {
 public:
  enum class Status {
    // Cloud state is compatible.
    OK,
    // Cloud state is not compatible, ie. it was erased without erasing the
    // local state on this device.
    INCOMPATIBLE,
    // Couldn't determine the compatibility due to a network error.
    NETWORK_ERROR,
    // Couldn't determine the compatibility due to a disk error.
    DISK_ERROR,
  };

  LocalVersionChecker(){};
  virtual ~LocalVersionChecker(){};

  // Verifies that the version assigned to this device in the device version map
  // hasn't changed since the previous time we checked (or sets it if empty).
  // This makes at most one network request using the given |auth_token|.
  virtual void CheckCloudVersion(std::string auth_token,
                                 firebase::Firebase* user_firebase,
                                 std::string local_version_path,
                                 std::function<void(Status)> callback) = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(LocalVersionChecker);
};

}  // namespace cloud_sync

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_PUBLIC_LOCAL_VERSION_CHECKER_H_
