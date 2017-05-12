// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_BENCHMARK_LIB_DATA_H_
#define APPS_LEDGER_BENCHMARK_LIB_DATA_H_

#include "lib/fidl/cpp/bindings/array.h"

namespace benchmark {

// Builds a key of the given length as "<the given int>-<random data>", so that
// deterministic ordering of entries can be ensured by using a different |i|
// value each time, but the resulting b-tree nodes are always distinct.
fidl::Array<uint8_t> MakeKey(int i, size_t size);

// Builds a random value of the given length.
fidl::Array<uint8_t> MakeValue(size_t size);

}  // namespace benchmark

#endif  // APPS_LEDGER_BENCHMARK_LIB_DATA_H_
