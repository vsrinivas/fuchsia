// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_FIDL_INCLUDE_TYPES_H_
#define PERIDOT_BIN_LEDGER_FIDL_INCLUDE_TYPES_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <fuchsia/ledger/internal/cpp/fidl.h>

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

namespace ledger_internal {
using Commit = fuchsia::ledger::internal::Commit;
using CommitId = fuchsia::ledger::internal::CommitId;
using CommitPtr = fuchsia::ledger::internal::CommitPtr;
using LedgerController = fuchsia::ledger::internal::LedgerController;
using LedgerControllerPtr = fuchsia::ledger::internal::LedgerControllerPtr;
using LedgerDebug = fuchsia::ledger::internal::LedgerDebug;
using LedgerDebugPtr = fuchsia::ledger::internal::LedgerDebugPtr;
using LedgerRepository = fuchsia::ledger::internal::LedgerRepository;
using LedgerRepositoryPtr = fuchsia::ledger::internal::LedgerRepositoryPtr;
using LedgerRepositoryDebug = fuchsia::ledger::internal::LedgerRepositoryDebug;
using LedgerRepositoryDebugPtr =
    fuchsia::ledger::internal::LedgerRepositoryDebugPtr;
using LedgerRepositoryFactory =
    fuchsia::ledger::internal::LedgerRepositoryFactory;
using LedgerRepositoryFactoryPtr =
    fuchsia::ledger::internal::LedgerRepositoryFactoryPtr;
using PageDebug = fuchsia::ledger::internal::PageDebug;
using PageDebugPtr = fuchsia::ledger::internal::PageDebugPtr;
}  // namespace ledger_internal

#endif  // PERIDOT_BIN_LEDGER_FIDL_INCLUDE_TYPES_H_
