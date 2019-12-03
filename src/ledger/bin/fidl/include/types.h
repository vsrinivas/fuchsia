// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_FIDL_INCLUDE_TYPES_H_
#define SRC_LEDGER_BIN_FIDL_INCLUDE_TYPES_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <fuchsia/ledger/cloud/test/cpp/fidl.h>
#include <fuchsia/ledger/cpp/fidl.h>
#include <fuchsia/ledger/internal/cpp/fidl.h>

// More convenient aliases for FIDL types.

namespace cloud_provider {
using ClockEntry = fuchsia::ledger::cloud::ClockEntry;
using DeletionEntry = fuchsia::ledger::cloud::DeletionEntry;
using TombstoneEntry = fuchsia::ledger::cloud::TombstoneEntry;
using DeviceEntry = fuchsia::ledger::cloud::DeviceEntry;
using DeviceClock = fuchsia::ledger::cloud::DeviceClock;
using Clock = fuchsia::ledger::cloud::Clock;
using ClockPack = fuchsia::ledger::cloud::ClockPack;
using CloudProvider = fuchsia::ledger::cloud::CloudProvider;
using CloudProviderPtr = fuchsia::ledger::cloud::CloudProviderPtr;
using CloudController = fuchsia::ledger::cloud::test::CloudController;
using CloudControllerPtr = fuchsia::ledger::cloud::test::CloudControllerPtr;
using CloudControllerFactory = fuchsia::ledger::cloud::test::CloudControllerFactory;
using CloudControllerFactoryPtr = fuchsia::ledger::cloud::test::CloudControllerFactoryPtr;
using CommitPack = fuchsia::ledger::cloud::CommitPack;
using Commit = fuchsia::ledger::cloud::Commit;
using Commits = fuchsia::ledger::cloud::Commits;
using DeviceSet = fuchsia::ledger::cloud::DeviceSet;
using DeviceSetPtr = fuchsia::ledger::cloud::DeviceSetPtr;
using DeviceSetWatcher = fuchsia::ledger::cloud::DeviceSetWatcher;
using DeviceSetWatcherPtr = fuchsia::ledger::cloud::DeviceSetWatcherPtr;
using Diff = fuchsia::ledger::cloud::Diff;
using DiffEntry = fuchsia::ledger::cloud::DiffEntry;
using DiffPack = fuchsia::ledger::cloud::DiffPack;
using DiffSupport = fuchsia::ledger::cloud::test::DiffSupport;
using NetworkState = fuchsia::ledger::cloud::test::NetworkState;
using Operation = fuchsia::ledger::cloud::Operation;
using PageCloud = fuchsia::ledger::cloud::PageCloud;
using PageCloudPtr = fuchsia::ledger::cloud::PageCloudPtr;
using PageCloudWatcher = fuchsia::ledger::cloud::PageCloudWatcher;
using PageCloudWatcherPtr = fuchsia::ledger::cloud::PageCloudWatcherPtr;
using PageState = fuchsia::ledger::cloud::PageState;
using PositionToken = fuchsia::ledger::cloud::PositionToken;
using ReferencePack = fuchsia::ledger::cloud::ReferencePack;
using Status = fuchsia::ledger::cloud::Status;
}  // namespace cloud_provider

namespace ledger {
using BytesOrReference = fuchsia::ledger::BytesOrReference;
using BytesOrReferencePtr = fuchsia::ledger::BytesOrReferencePtr;
using ConflictResolutionWaitStatus = fuchsia::ledger::ConflictResolutionWaitStatus;
using ConflictResolver = fuchsia::ledger::ConflictResolver;
using ConflictResolverFactory = fuchsia::ledger::ConflictResolverFactory;
using ConflictResolverFactoryPtr = fuchsia::ledger::ConflictResolverFactoryPtr;
using ConflictResolverPtr = fuchsia::ledger::ConflictResolverPtr;
using DiffEntry = fuchsia::ledger::DiffEntry;
using Entry = fuchsia::ledger::Entry;
using Error = fuchsia::ledger::Error;
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
using ResultState = fuchsia::ledger::ResultState;
using SyncState = fuchsia::ledger::SyncState;
using SyncWatcher = fuchsia::ledger::SyncWatcher;
using SyncWatcherPtr = fuchsia::ledger::SyncWatcherPtr;
using Token = fuchsia::ledger::Token;
using Value = fuchsia::ledger::Value;
using ValuePtr = fuchsia::ledger::ValuePtr;
using ValueSource = fuchsia::ledger::ValueSource;
}  // namespace ledger

namespace ledger_internal {
using LedgerController = fuchsia::ledger::internal::LedgerController;
using LedgerControllerPtr = fuchsia::ledger::internal::LedgerControllerPtr;
using LedgerRepository = fuchsia::ledger::internal::LedgerRepository;
using LedgerRepositoryPtr = fuchsia::ledger::internal::LedgerRepositoryPtr;
using LedgerRepositoryFactory = fuchsia::ledger::internal::LedgerRepositoryFactory;
using LedgerRepositoryFactoryPtr = fuchsia::ledger::internal::LedgerRepositoryFactoryPtr;
}  // namespace ledger_internal

#endif  // SRC_LEDGER_BIN_FIDL_INCLUDE_TYPES_H_
