// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTS_INTEGRATION_TEST_UTILS_H_
#define PERIDOT_BIN_LEDGER_TESTS_INTEGRATION_TEST_UTILS_H_

#include <string>
#include <vector>

#include <fuchsia/cpp/ledger.h>
#include "lib/fsl/vmo/sized_vmo.h"

namespace test {
namespace integration {

fidl::VectorPtr<uint8_t> RandomArray(size_t size,
                                     const std::vector<uint8_t>& prefix);

fidl::VectorPtr<uint8_t> RandomArray(int size);

ledger::PageId PageGetId(ledger::PagePtr* page);

ledger::PageSnapshotPtr PageGetSnapshot(
    ledger::PagePtr* page,
    fidl::VectorPtr<uint8_t> prefix = nullptr);

fidl::VectorPtr<fidl::VectorPtr<uint8_t>> SnapshotGetKeys(
    ledger::PageSnapshotPtr* snapshot,
    fidl::VectorPtr<uint8_t> start);
fidl::VectorPtr<fidl::VectorPtr<uint8_t>> SnapshotGetKeys(
    ledger::PageSnapshotPtr* snapshot,
    fidl::VectorPtr<uint8_t> start,
    int* num_queries);

fidl::VectorPtr<ledger::Entry> SnapshotGetEntries(
    ledger::PageSnapshotPtr* snapshot,
    fidl::VectorPtr<uint8_t> start);
fidl::VectorPtr<ledger::Entry> SnapshotGetEntries(
    ledger::PageSnapshotPtr* snapshot,
    fidl::VectorPtr<uint8_t> start,
    int* num_queries);

std::string SnapshotFetchPartial(ledger::PageSnapshotPtr* snapshot,
                                 fidl::VectorPtr<uint8_t> key,
                                 int64_t offset,
                                 int64_t max_size);

std::string ToString(const mem::BufferPtr& vmo);
fidl::VectorPtr<uint8_t> ToArray(const mem::BufferPtr& vmo);

}  // namespace integration
}  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TESTS_INTEGRATION_TEST_UTILS_H_
