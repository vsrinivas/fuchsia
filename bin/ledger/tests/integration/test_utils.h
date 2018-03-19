// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTS_INTEGRATION_TEST_UTILS_H_
#define PERIDOT_BIN_LEDGER_TESTS_INTEGRATION_TEST_UTILS_H_

#include <string>
#include <vector>

#include "lib/fsl/vmo/sized_vmo.h"
#include "lib/ledger/fidl/ledger.fidl.h"

namespace test {
namespace integration {

f1dl::VectorPtr<uint8_t> RandomArray(size_t size,
                                 const std::vector<uint8_t>& prefix);

f1dl::VectorPtr<uint8_t> RandomArray(int size);

f1dl::VectorPtr<uint8_t> PageGetId(ledger::PagePtr* page);

ledger::PageSnapshotPtr PageGetSnapshot(ledger::PagePtr* page,
                                        f1dl::VectorPtr<uint8_t> prefix = nullptr);

f1dl::VectorPtr<f1dl::VectorPtr<uint8_t>> SnapshotGetKeys(
    ledger::PageSnapshotPtr* snapshot,
    f1dl::VectorPtr<uint8_t> start);
f1dl::VectorPtr<f1dl::VectorPtr<uint8_t>> SnapshotGetKeys(
    ledger::PageSnapshotPtr* snapshot,
    f1dl::VectorPtr<uint8_t> start,
    int* num_queries);

f1dl::VectorPtr<ledger::EntryPtr> SnapshotGetEntries(
    ledger::PageSnapshotPtr* snapshot,
    f1dl::VectorPtr<uint8_t> start);
f1dl::VectorPtr<ledger::EntryPtr> SnapshotGetEntries(
    ledger::PageSnapshotPtr* snapshot,
    f1dl::VectorPtr<uint8_t> start,
    int* num_queries);

std::string SnapshotFetchPartial(ledger::PageSnapshotPtr* snapshot,
                                 f1dl::VectorPtr<uint8_t> key,
                                 int64_t offset,
                                 int64_t max_size);

std::string ToString(const fsl::SizedVmoTransportPtr& vmo);
f1dl::VectorPtr<uint8_t> ToArray(const fsl::SizedVmoTransportPtr& vmo);

}  // namespace integration
}  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TESTS_INTEGRATION_TEST_UTILS_H_
