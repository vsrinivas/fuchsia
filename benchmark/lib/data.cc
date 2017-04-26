// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/benchmark/lib/data.h"

#include <string>

#include "apps/ledger/benchmark/lib/convert.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/random/rand.h"
#include "lib/ftl/strings/concatenate.h"

namespace benchmark {

// Builds a key as "<the given int>-<random data>", so that deterministic
// ordering of entries can be ensured by using a different |i| value each time,
// but the resultin b-tree nodes are always distinct.
fidl::Array<uint8_t> MakeKey(int i) {
  return ToArray(ftl::Concatenate(
      {std::to_string(i), "-", std::to_string(ftl::RandUint64())}));
}

// Builds a random value of the given length.
fidl::Array<uint8_t> MakeValue(size_t size) {
  auto data = fidl::Array<uint8_t>::New(size);
  auto ret = ftl::RandBytes(data.data(), size);
  FTL_DCHECK(ret);
  return data;
}

}  // namespace benchmark
