// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TEST_INTEGRATION_TEST_UTILS_H_
#define PERIDOT_BIN_LEDGER_TEST_INTEGRATION_TEST_UTILS_H_

#include <string>
#include <vector>

#include "lib/ledger/fidl/ledger.fidl.h"
#include "zx/vmo.h"

namespace test {
namespace integration {

fidl::Array<uint8_t> RandomArray(size_t size,
                                 const std::vector<uint8_t>& prefix);

fidl::Array<uint8_t> RandomArray(int size);

fidl::Array<uint8_t> PageGetId(ledger::PagePtr* page);

ledger::PageSnapshotPtr PageGetSnapshot(ledger::PagePtr* page,
                                        fidl::Array<uint8_t> prefix = nullptr);

fidl::Array<fidl::Array<uint8_t>> SnapshotGetKeys(
    ledger::PageSnapshotPtr* snapshot,
    fidl::Array<uint8_t> start);
fidl::Array<fidl::Array<uint8_t>> SnapshotGetKeys(
    ledger::PageSnapshotPtr* snapshot,
    fidl::Array<uint8_t> start,
    int* num_queries);

fidl::Array<ledger::EntryPtr> SnapshotGetEntries(
    ledger::PageSnapshotPtr* snapshot,
    fidl::Array<uint8_t> start);
fidl::Array<ledger::EntryPtr> SnapshotGetEntries(
    ledger::PageSnapshotPtr* snapshot,
    fidl::Array<uint8_t> start,
    int* num_queries);

std::string SnapshotFetchPartial(ledger::PageSnapshotPtr* snapshot,
                                 fidl::Array<uint8_t> key,
                                 int64_t offset,
                                 int64_t max_size);

std::string ToString(const zx::vmo& vmo);

fidl::Array<uint8_t> ToArray(const zx::vmo& vmo);

}  // namespace integration
}  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TEST_INTEGRATION_TEST_UTILS_H_
