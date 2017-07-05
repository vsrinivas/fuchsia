// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_LOCAL_VERSION_CHECKER_IMPL_H_
#define APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_LOCAL_VERSION_CHECKER_IMPL_H_

#include <functional>
#include <string>

#include "apps/ledger/src/cloud_sync/public/local_version_checker.h"
#include "apps/ledger/src/firebase/firebase.h"

namespace cloud_sync {

class LocalVersionCheckerImpl : public LocalVersionChecker {
 public:
  LocalVersionCheckerImpl();
  ~LocalVersionCheckerImpl() override;

  void CheckCloudVersion(std::string auth_token,
                         firebase::Firebase* user_firebase,
                         std::string local_version_path,
                         std::function<void(Status)> callback) override;
};

}  // namespace cloud_sync

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_LOCAL_VERSION_CHECKER_IMPL_H_
