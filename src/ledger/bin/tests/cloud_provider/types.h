// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTS_CLOUD_PROVIDER_TYPES_H_
#define SRC_LEDGER_BIN_TESTS_CLOUD_PROVIDER_TYPES_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>

// More convenient aliases for FIDL types.

namespace cloud_provider {
using CloudProvider = fuchsia::ledger::cloud::CloudProvider;
using CloudProviderPtr = fuchsia::ledger::cloud::CloudProviderPtr;
using CloudProviderSyncPtr = fuchsia::ledger::cloud::CloudProviderSyncPtr;
using CommitPack = fuchsia::ledger::cloud::CommitPack;
using DeviceSet = fuchsia::ledger::cloud::DeviceSet;
using DeviceSetPtr = fuchsia::ledger::cloud::DeviceSetPtr;
using DeviceSetSyncPtr = fuchsia::ledger::cloud::DeviceSetSyncPtr;
using DeviceSetWatcher = fuchsia::ledger::cloud::DeviceSetWatcher;
using DeviceSetWatcherPtr = fuchsia::ledger::cloud::DeviceSetWatcherPtr;
using PageCloud = fuchsia::ledger::cloud::PageCloud;
using PageCloudPtr = fuchsia::ledger::cloud::PageCloudPtr;
using PageCloudSyncPtr = fuchsia::ledger::cloud::PageCloudSyncPtr;
using PageCloudWatcher = fuchsia::ledger::cloud::PageCloudWatcher;
using PageCloudWatcherPtr = fuchsia::ledger::cloud::PageCloudWatcherPtr;
using PositionToken = fuchsia::ledger::cloud::PositionToken;
using Status = fuchsia::ledger::cloud::Status;
}  // namespace cloud_provider

#endif  // SRC_LEDGER_BIN_TESTS_CLOUD_PROVIDER_TYPES_H_
