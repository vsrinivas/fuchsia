// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_INCLUDE_TYPES_H_
#define SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_INCLUDE_TYPES_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <fuchsia/ledger/cloud/firestore/cpp/fidl.h>

// More convenient aliases for FIDL types.

namespace cloud_provider {
using ClockPack = fuchsia::ledger::cloud::ClockPack;
using CloudProvider = fuchsia::ledger::cloud::CloudProvider;
using CloudProviderPtr = fuchsia::ledger::cloud::CloudProviderPtr;
using CommitPack = fuchsia::ledger::cloud::CommitPack;
using DeviceSet = fuchsia::ledger::cloud::DeviceSet;
using DeviceSetPtr = fuchsia::ledger::cloud::DeviceSetPtr;
using DeviceSetWatcher = fuchsia::ledger::cloud::DeviceSetWatcher;
using DeviceSetWatcherPtr = fuchsia::ledger::cloud::DeviceSetWatcherPtr;
using PageCloud = fuchsia::ledger::cloud::PageCloud;
using PageCloudPtr = fuchsia::ledger::cloud::PageCloudPtr;
using PageCloudWatcher = fuchsia::ledger::cloud::PageCloudWatcher;
using PageCloudWatcherPtr = fuchsia::ledger::cloud::PageCloudWatcherPtr;
using PositionToken = fuchsia::ledger::cloud::PositionToken;
using ReferencePack = fuchsia::ledger::cloud::ReferencePack;
using Status = fuchsia::ledger::cloud::Status;
}  // namespace cloud_provider

namespace cloud_provider_firestore {
using Config = fuchsia::ledger::cloud::firestore::Config;
using Factory = fuchsia::ledger::cloud::firestore::Factory;
using FactoryPtr = fuchsia::ledger::cloud::firestore::FactoryPtr;
}  // namespace cloud_provider_firestore

#endif  // SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_INCLUDE_TYPES_H_
