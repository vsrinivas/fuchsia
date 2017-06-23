// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_SYNC_PUBLIC_USER_CONFIG_H_
#define APPS_LEDGER_SRC_CLOUD_SYNC_PUBLIC_USER_CONFIG_H_

#include <memory>
#include <string>

#include "apps/ledger/src/cloud_sync/public/auth_provider.h"

namespace cloud_sync {

// Sync configuration for a particular user.
struct UserConfig {
  bool use_sync = false;
  // The id of the Firebase instance.
  std::string server_id;
  // The id of the user.
  std::string user_id;
  // The directory for the user persistent data.
  std::string user_directory;
  // The provider of the auth data for the user.
  AuthProvider* auth_provider;
};

}  // namespace cloud_sync

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_PUBLIC_USER_CONFIG_H_
