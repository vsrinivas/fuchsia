// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_CLOUD_SYNC_PUBLIC_USER_CONFIG_H_
#define PERIDOT_BIN_LEDGER_CLOUD_SYNC_PUBLIC_USER_CONFIG_H_

#include <memory>
#include <string>

#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "peridot/bin/ledger/device_set/cloud_device_set.h"

namespace cloud_sync {

// Sync configuration for a particular user.
struct UserConfig {
  // The directory for the user persistent data.
  std::string user_directory;
  // The provider of the auth data for the user.
  cloud_provider::CloudProviderPtr cloud_provider;
};

}  // namespace cloud_sync

#endif  // PERIDOT_BIN_LEDGER_CLOUD_SYNC_PUBLIC_USER_CONFIG_H_
