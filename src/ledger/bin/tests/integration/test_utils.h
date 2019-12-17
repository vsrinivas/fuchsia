// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTS_INTEGRATION_TEST_UTILS_H_
#define SRC_LEDGER_BIN_TESTS_INTEGRATION_TEST_UTILS_H_

#include <string>
#include <vector>

#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/testing/ledger_app_instance_factory.h"
#include "src/ledger/lib/rng/random.h"
#include "src/ledger/lib/vmo/sized_vmo.h"

namespace ledger {

// Builds an array of length |size|, starting with |prefix| and completed with
// random data.
std::vector<uint8_t> RandomArray(Random* random, size_t size,
                                 const std::vector<uint8_t>& prefix = {});

// Extracts the content of |vmo| as a std::string.
std::string ToString(const fuchsia::mem::BufferPtr& vmo);

// Extracts the content of |vmo| as a FIDL vector.
std::vector<uint8_t> ToArray(const fuchsia::mem::BufferPtr& vmo);

// Retrieves all entries from the snapshot with a key greater of equals to
// |start|. If |num_queries| is not null, returns the number of calls to
// |GetEntries|. If any call fails, this function will fail the current test.
std::vector<Entry> SnapshotGetEntries(LoopController* loop_controller, PageSnapshotPtr* snapshot,
                                      std::vector<uint8_t> start = {}, int* num_queries = nullptr);

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTS_INTEGRATION_TEST_UTILS_H_
