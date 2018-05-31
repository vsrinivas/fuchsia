// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_FIDL_INCLUDE_TYPES_H_
#define PERIDOT_BIN_LEDGER_FIDL_INCLUDE_TYPES_H_

#include <fuchsia/ledger/cloud/firebase/cpp/fidl.h>

// More convenient aliases for FIDL types.

namespace cloud_provider {
using CloudProvider = fuchsia::ledger::cloud::CloudProvider;
using CloudProviderPtr = fuchsia::ledger::cloud::CloudProviderPtr;
using Commit = fuchsia::ledger::cloud::Commit;
using DeviceSet = fuchsia::ledger::cloud::DeviceSet;
using DeviceSetPtr = fuchsia::ledger::cloud::DeviceSetPtr;
using DeviceSetWatcher = fuchsia::ledger::cloud::DeviceSetWatcher;
using DeviceSetWatcherPtr = fuchsia::ledger::cloud::DeviceSetWatcherPtr;
using PageCloud = fuchsia::ledger::cloud::PageCloud;
using PageCloudPtr = fuchsia::ledger::cloud::PageCloudPtr;
using PageCloudWatcher = fuchsia::ledger::cloud::PageCloudWatcher;
using PageCloudWatcherPtr = fuchsia::ledger::cloud::PageCloudWatcherPtr;
using Status = fuchsia::ledger::cloud::Status;
}  // namespace cloud_provider

#endif  // PERIDOT_BIN_LEDGER_FIDL_INCLUDE_TYPES_H_
