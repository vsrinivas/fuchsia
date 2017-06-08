// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_LOCAL_VERSION_CHECKER_H_
#define APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_LOCAL_VERSION_CHECKER_H_

#include <functional>
#include <string>

#include "apps/ledger/src/firebase/firebase.h"

namespace cloud_sync {

class LocalVersionChecker {
 public:
  enum class Status {
    OK,
    INCOMPATIBLE,
    NETWORK_ERROR,
    DISK_ERROR,
  };

  LocalVersionChecker();
  ~LocalVersionChecker();

  void CheckCloudVersion(std::string auth_token,
                         firebase::Firebase* user_firebase,
                         std::string local_version_path,
                         std::function<void(Status)> callback);

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(LocalVersionChecker);
};

}  // namespace cloud_sync

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_LOCAL_VERSION_CHECKER_H_
