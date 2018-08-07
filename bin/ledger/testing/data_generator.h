// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTING_DATA_GENERATOR_H_
#define PERIDOT_BIN_LEDGER_TESTING_DATA_GENERATOR_H_

#include <climits>
#include <random>
#include <vector>

#include <lib/fidl/cpp/vector.h>

#include "peridot/bin/ledger/fidl/include/types.h"

namespace ledger {

class DataGenerator {
 public:
  DataGenerator();

  explicit DataGenerator(uint64_t seed);

  ~DataGenerator();

  // Builds a key of the given length as "<the given int>-<random data>", so
  // that deterministic ordering of entries can be ensured by using a different
  // |i| value each time, but the resulting B-tree nodes are always distinct.
  fidl::VectorPtr<uint8_t> MakeKey(int i, size_t size);

  // Builds a random value that can be used as a page id.
  PageId MakePageId();

  // Builds a random value of the given length.
  fidl::VectorPtr<uint8_t> MakeValue(size_t size);

  // Builds a vector of length |key_count| containing keys of size |key_size|,
  // |unique_key_count| of which are unique.
  std::vector<fidl::VectorPtr<uint8_t>> MakeKeys(size_t key_count,
                                                 size_t key_size,
                                                 size_t unique_key_count);

 private:
  std::independent_bits_engine<std::default_random_engine, CHAR_BIT, uint8_t>
      generator_;
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_TESTING_DATA_GENERATOR_H_
