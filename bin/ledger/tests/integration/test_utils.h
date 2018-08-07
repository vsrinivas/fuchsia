// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTS_INTEGRATION_TEST_UTILS_H_
#define PERIDOT_BIN_LEDGER_TESTS_INTEGRATION_TEST_UTILS_H_

#include <string>
#include <vector>

#include <lib/fsl/vmo/sized_vmo.h>

#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/testing/ledger_app_instance_factory.h"

namespace ledger {

// Builds an array of length |size|, starting with |prefix| and completed with
// random data.
fidl::VectorPtr<uint8_t> RandomArray(size_t size,
                                     const std::vector<uint8_t>& prefix = {});

// Extracts the content of |vmo| as a std::string.
std::string ToString(const fuchsia::mem::BufferPtr& vmo);

// Extracts the content of |vmo| as a FIDL vector.
fidl::VectorPtr<uint8_t> ToArray(const fuchsia::mem::BufferPtr& vmo);

// Retrieves all entries from the snapshot with a key greater of equals to
// |start|. If |num_queries| is not null, returns the number of calls to
// |GetEntries|. If any call fails, this function will fail the current test.
std::vector<Entry> SnapshotGetEntries(
    LedgerAppInstanceFactory::LoopController* loop_controller,
    PageSnapshotPtr* snapshot,
    fidl::VectorPtr<uint8_t> start = fidl::VectorPtr<uint8_t>::New(0),
    int* num_queries = nullptr);

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_TESTS_INTEGRATION_TEST_UTILS_H_
