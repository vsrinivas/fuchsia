// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_CLOUD_SYNC_PUBLIC_USER_CONFIG_H_
#define SRC_LEDGER_BIN_CLOUD_SYNC_PUBLIC_USER_CONFIG_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>

#include <memory>
#include <string>

#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/platform/detached_path.h"

namespace cloud_sync {

// Sync configuration for a particular user.
struct UserConfig {
  // The directory for the user persistent data.
  ledger::DetachedPath user_directory;
  // The provider of the auth data for the user.
  cloud_provider::CloudProviderPtr cloud_provider;
};

}  // namespace cloud_sync

#endif  // SRC_LEDGER_BIN_CLOUD_SYNC_PUBLIC_USER_CONFIG_H_
