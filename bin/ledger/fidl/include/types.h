// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_FIDL_INCLUDE_TYPES_H_
#define PERIDOT_BIN_LEDGER_FIDL_INCLUDE_TYPES_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <fuchsia/ledger/cpp/fidl.h>
#include <fuchsia/ledger/internal/cpp/fidl.h>

// More convenient aliases for FIDL types.

namespace cloud_provider {
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
using Status = fuchsia::ledger::cloud::Status;
using Token = fuchsia::ledger::cloud::Token;
}  // namespace cloud_provider

namespace ledger {
using BytesOrReference = fuchsia::ledger::BytesOrReference;
using BytesOrReferencePtr = fuchsia::ledger::BytesOrReferencePtr;
using ConflictResolutionWaitStatus =
    fuchsia::ledger::ConflictResolutionWaitStatus;
using ConflictResolver = fuchsia::ledger::ConflictResolver;
using ConflictResolverFactory = fuchsia::ledger::ConflictResolverFactory;
using ConflictResolverFactoryPtr = fuchsia::ledger::ConflictResolverFactoryPtr;
using ConflictResolverPtr = fuchsia::ledger::ConflictResolverPtr;
using DiffEntry = fuchsia::ledger::DiffEntry;
using Entry = fuchsia::ledger::Entry;
using InlinedEntry = fuchsia::ledger::InlinedEntry;
using InlinedValue = fuchsia::ledger::InlinedValue;
using InlinedValuePtr = fuchsia::ledger::InlinedValuePtr;
using Ledger = fuchsia::ledger::Ledger;
using LedgerPtr = fuchsia::ledger::LedgerPtr;
using MergePolicy = fuchsia::ledger::MergePolicy;
using MergeResultProvider = fuchsia::ledger::MergeResultProvider;
using MergeResultProviderPtr = fuchsia::ledger::MergeResultProviderPtr;
using MergedValue = fuchsia::ledger::MergedValue;
using Page = fuchsia::ledger::Page;
using PageChange = fuchsia::ledger::PageChange;
using PageChangePtr = fuchsia::ledger::PageChangePtr;
using PageId = fuchsia::ledger::PageId;
using PageIdPtr = fuchsia::ledger::PageIdPtr;
using PagePtr = fuchsia::ledger::PagePtr;
using PageSnapshot = fuchsia::ledger::PageSnapshot;
using PageSnapshotPtr = fuchsia::ledger::PageSnapshotPtr;
using PageWatcher = fuchsia::ledger::PageWatcher;
using PageWatcherPtr = fuchsia::ledger::PageWatcherPtr;
using Priority = fuchsia::ledger::Priority;
using Reference = fuchsia::ledger::Reference;
using ReferencePtr = fuchsia::ledger::ReferencePtr;
using ResultState = fuchsia::ledger::ResultState;
using Status = fuchsia::ledger::Status;
using SyncState = fuchsia::ledger::SyncState;
using SyncWatcher = fuchsia::ledger::SyncWatcher;
using SyncWatcherPtr = fuchsia::ledger::SyncWatcherPtr;
using Token = fuchsia::ledger::Token;
using Value = fuchsia::ledger::Value;
using ValuePtr = fuchsia::ledger::ValuePtr;
using ValueSource = fuchsia::ledger::ValueSource;
}  // namespace ledger

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
