// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/tests/integration/test_utils.h"

#include <string>
#include <utility>
#include <vector>

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/files/scoped_temp_dir.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/time/time_delta.h>
#include <lib/zx/time.h>

#include "garnet/public/lib/callback/capture.h"
#include "gtest/gtest.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/lib/convert/convert.h"

namespace test {
namespace integration {

fidl::VectorPtr<uint8_t> RandomArray(size_t size,
                                     const std::vector<uint8_t>& prefix) {
  EXPECT_TRUE(size >= prefix.size());
  fidl::VectorPtr<uint8_t> array = fidl::VectorPtr<uint8_t>::New(size);
  for (size_t i = 0; i < prefix.size(); ++i) {
    array->at(i) = prefix[i];
  }
  for (size_t i = prefix.size(); i < size / 4; ++i) {
    int random = std::rand();
    for (size_t j = 0; j < 4 && 4 * i + j < size; ++j) {
      array->at(4 * i + j) = random & 0xFF;
      random = random >> 8;
    }
  }
  return array;
}

std::string ToString(const fuchsia::mem::BufferPtr& vmo) {
  std::string value;
  bool status = fsl::StringFromVmo(*vmo, &value);
  FXL_DCHECK(status);
  return value;
}

fidl::VectorPtr<uint8_t> ToArray(const fuchsia::mem::BufferPtr& vmo) {
  return convert::ToArray(ToString(vmo));
}

std::vector<ledger::Entry> SnapshotGetEntries(
    LedgerAppInstanceFactory::LoopController* loop_controller,
    ledger::PageSnapshotPtr* snapshot, fidl::VectorPtr<uint8_t> start,
    int* num_queries) {
  std::vector<ledger::Entry> result;
  std::unique_ptr<ledger::Token> token;
  if (num_queries) {
    *num_queries = 0;
  }
  do {
    ledger::Status status;
    fidl::VectorPtr<ledger::Entry> entries;
    auto waiter = loop_controller->NewWaiter();
    (*snapshot)->GetEntries(
        start.Clone(), std::move(token),
        callback::Capture(waiter->GetCallback(), &status, &entries, &token));
    waiter->RunUntilCalled();
    EXPECT_TRUE(status == ledger::Status::OK ||
                status == ledger::Status::PARTIAL_RESULT)
        << "Actual status: " << fidl::ToUnderlying(status);
    if (num_queries) {
      (*num_queries)++;
    }
    for (auto& entry : entries.take()) {
      result.push_back(std::move(entry));
    }
  } while (token);
  return result;
}

}  // namespace integration
}  // namespace test
