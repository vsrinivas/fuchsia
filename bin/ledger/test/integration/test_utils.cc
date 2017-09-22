// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/test/integration/test_utils.h"

#include <string>
#include <utility>
#include <vector>

#include "apps/ledger/services/internal/internal.fidl.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/app/ledger_repository_factory_impl.h"
#include "apps/ledger/src/convert/convert.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/time/time_delta.h"

namespace test {
namespace integration {

fidl::Array<uint8_t> RandomArray(size_t size,
                                 const std::vector<uint8_t>& prefix) {
  EXPECT_TRUE(size >= prefix.size());
  fidl::Array<uint8_t> array = fidl::Array<uint8_t>::New(size);
  for (size_t i = 0; i < prefix.size(); ++i) {
    array[i] = prefix[i];
  }
  for (size_t i = prefix.size(); i < size / 4; ++i) {
    int random = std::rand();
    for (size_t j = 0; j < 4 && 4 * i + j < size; ++j) {
      array[4 * i + j] = random & 0xFF;
      random = random >> 8;
    }
  }
  return array;
}

fidl::Array<uint8_t> RandomArray(int size) {
  return RandomArray(size, std::vector<uint8_t>());
}

fidl::Array<uint8_t> PageGetId(ledger::PagePtr* page) {
  fidl::Array<uint8_t> page_id;
  (*page)->GetId(
      [&page_id](fidl::Array<uint8_t> id) { page_id = std::move(id); });
  EXPECT_TRUE(
      page->WaitForIncomingResponseWithTimeout(fxl::TimeDelta::FromSeconds(1)));
  return page_id;
}

ledger::PageSnapshotPtr PageGetSnapshot(ledger::PagePtr* page,
                                        fidl::Array<uint8_t> prefix) {
  ledger::PageSnapshotPtr snapshot;
  (*page)->GetSnapshot(
      snapshot.NewRequest(), std::move(prefix), nullptr,
      [](ledger::Status status) { EXPECT_EQ(ledger::Status::OK, status); });
  EXPECT_TRUE(
      page->WaitForIncomingResponseWithTimeout(fxl::TimeDelta::FromSeconds(1)));
  return snapshot;
}

fidl::Array<fidl::Array<uint8_t>> SnapshotGetKeys(
    ledger::PageSnapshotPtr* snapshot,
    fidl::Array<uint8_t> start) {
  return SnapshotGetKeys(snapshot, std::move(start), nullptr);
}

fidl::Array<fidl::Array<uint8_t>> SnapshotGetKeys(
    ledger::PageSnapshotPtr* snapshot,
    fidl::Array<uint8_t> start,
    int* num_queries) {
  fidl::Array<fidl::Array<uint8_t>> result;
  fidl::Array<uint8_t> token = nullptr;
  fidl::Array<uint8_t> next_token = nullptr;
  if (num_queries) {
    *num_queries = 0;
  }
  do {
    (*snapshot)->GetKeys(
        start.Clone(), std::move(token),
        [&result, &next_token, &num_queries](
            ledger::Status status, fidl::Array<fidl::Array<uint8_t>> keys,
            fidl::Array<uint8_t> new_next_token) {
          EXPECT_TRUE(status == ledger::Status::OK ||
                      status == ledger::Status::PARTIAL_RESULT);
          if (num_queries) {
            (*num_queries)++;
          }
          for (auto& key : keys) {
            result.push_back(std::move(key));
          }
          next_token = std::move(new_next_token);
        });
    EXPECT_TRUE(snapshot->WaitForIncomingResponseWithTimeout(
        fxl::TimeDelta::FromSeconds(1)));
    token = std::move(next_token);
    next_token = nullptr;  // Suppress misc-use-after-move.
  } while (token);
  return result;
}

fidl::Array<ledger::EntryPtr> SnapshotGetEntries(
    ledger::PageSnapshotPtr* snapshot,
    fidl::Array<uint8_t> start) {
  return SnapshotGetEntries(snapshot, std::move(start), nullptr);
}

fidl::Array<ledger::EntryPtr> SnapshotGetEntries(
    ledger::PageSnapshotPtr* snapshot,
    fidl::Array<uint8_t> start,
    int* num_queries) {
  fidl::Array<ledger::EntryPtr> result;
  fidl::Array<uint8_t> token = nullptr;
  fidl::Array<uint8_t> next_token = nullptr;
  if (num_queries) {
    *num_queries = 0;
  }
  do {
    (*snapshot)->GetEntries(
        start.Clone(), std::move(token),
        [&result, &next_token, &num_queries](
            ledger::Status status, fidl::Array<ledger::EntryPtr> entries,
            fidl::Array<uint8_t> new_next_token) {
          EXPECT_TRUE(status == ledger::Status::OK ||
                      status == ledger::Status::PARTIAL_RESULT)
              << "Actual status: " << status;
          if (num_queries) {
            (*num_queries)++;
          }
          for (auto& entry : entries) {
            result.push_back(std::move(entry));
          }
          next_token = std::move(new_next_token);
        });
    EXPECT_TRUE(snapshot->WaitForIncomingResponseWithTimeout(
        fxl::TimeDelta::FromSeconds(1)));
    token = std::move(next_token);
    next_token = nullptr;  // Suppress misc-use-after-move.
  } while (token);
  return result;
}

std::string ToString(const zx::vmo& vmo) {
  std::string value;
  bool status = fsl::StringFromVmo(vmo, &value);
  FXL_DCHECK(status);
  return value;
}

fidl::Array<uint8_t> ToArray(const zx::vmo& vmo) {
  return convert::ToArray(ToString(vmo));
}

std::string SnapshotFetchPartial(ledger::PageSnapshotPtr* snapshot,
                                 fidl::Array<uint8_t> key,
                                 int64_t offset,
                                 int64_t max_size) {
  std::string result;
  (*snapshot)->FetchPartial(std::move(key), offset, max_size,
                            [&result](ledger::Status status, zx::vmo buffer) {
                              EXPECT_EQ(status, ledger::Status::OK);
                              EXPECT_TRUE(fsl::StringFromVmo(buffer, &result));
                            });
  EXPECT_TRUE(snapshot->WaitForIncomingResponseWithTimeout(
      fxl::TimeDelta::FromSeconds(1)));
  return result;
}

}  // namespace integration
}  // namespace test
