// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/tests/integration/test_utils.h"

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/zx/time.h>

#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/lib/callback/capture.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/lib/vmo/strings.h"

namespace ledger {

std::vector<uint8_t> RandomArray(rng::Random* random, size_t size,
                                 const std::vector<uint8_t>& prefix) {
  EXPECT_TRUE(size >= prefix.size());
  std::vector<uint8_t> array(size);
  for (size_t i = 0; i < prefix.size(); ++i) {
    array.at(i) = prefix[i];
  }
  random->Draw(&array[prefix.size()], size - prefix.size());
  return array;
}

std::string ToString(const fuchsia::mem::BufferPtr& vmo) {
  std::string value;
  bool status = StringFromVmo(*vmo, &value);
  LEDGER_DCHECK(status);
  return value;
}

std::vector<uint8_t> ToArray(const fuchsia::mem::BufferPtr& vmo) {
  return convert::ToArray(ToString(vmo));
}

std::vector<Entry> SnapshotGetEntries(LoopController* loop_controller, PageSnapshotPtr* snapshot,
                                      std::vector<uint8_t> start, int* num_queries) {
  std::vector<Entry> result;
  std::unique_ptr<Token> token;
  if (num_queries) {
    *num_queries = 0;
  }
  do {
    std::vector<Entry> entries;
    auto waiter = loop_controller->NewWaiter();
    (*snapshot)->GetEntries(fidl::Clone(start), std::move(token),
                            Capture(waiter->GetCallback(), &entries, &token));
    if (!waiter->RunUntilCalled()) {
      ADD_FAILURE() << "|GetEntries| failed to call back.";
      return {};
    }
    if (num_queries) {
      (*num_queries)++;
    }
    for (auto& entry : entries) {
      result.push_back(std::move(entry));
    }
  } while (token);
  return result;
}

}  // namespace ledger
