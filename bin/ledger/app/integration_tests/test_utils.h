// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_INTEGRATION_TESTS_TEST_UTILS_H_
#define APPS_LEDGER_SRC_APP_INTEGRATION_TESTS_TEST_UTILS_H_

#include <string>
#include <vector>

#include "apps/ledger/services/public/ledger.fidl.h"
#include "mx/vmo.h"

namespace ledger {
namespace integration_tests {

fidl::Array<uint8_t> RandomArray(size_t size,
                                 const std::vector<uint8_t>& prefix);

fidl::Array<uint8_t> RandomArray(int size);

fidl::Array<uint8_t> PageGetId(PagePtr* page);

PageSnapshotPtr PageGetSnapshot(PagePtr* page,
                                fidl::Array<uint8_t> prefix = nullptr);

fidl::Array<fidl::Array<uint8_t>> SnapshotGetKeys(PageSnapshotPtr* snapshot,
                                                  fidl::Array<uint8_t> start);
fidl::Array<fidl::Array<uint8_t>> SnapshotGetKeys(PageSnapshotPtr* snapshot,
                                                  fidl::Array<uint8_t> start,
                                                  int* num_queries);

fidl::Array<EntryPtr> SnapshotGetEntries(PageSnapshotPtr* snapshot,
                                         fidl::Array<uint8_t> start);
fidl::Array<EntryPtr> SnapshotGetEntries(PageSnapshotPtr* snapshot,
                                         fidl::Array<uint8_t> start,
                                         int* num_queries);

std::string SnapshotFetchPartial(PageSnapshotPtr* snapshot,
                                 fidl::Array<uint8_t> key,
                                 int64_t offset,
                                 int64_t max_size);

std::string ToString(const mx::vmo& vmo);

fidl::Array<uint8_t> ToArray(const mx::vmo& vmo);

}  // namespace integration_tests
}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_INTEGRATION_TESTS_TEST_UTILS_H_
