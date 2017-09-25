// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TEST_DATA_GENERATOR_H_
#define PERIDOT_BIN_LEDGER_TEST_DATA_GENERATOR_H_

#include <climits>
#include <random>

#include "lib/fidl/cpp/bindings/array.h"

namespace test {

class DataGenerator {
 public:
  DataGenerator();

  explicit DataGenerator(uint64_t seed);

  ~DataGenerator();

  // Builds a key of the given length as "<the given int>-<random data>", so
  // that deterministic ordering of entries can be ensured by using a different
  // |i| value each time, but the resulting B-tree nodes are always distinct.
  fidl::Array<uint8_t> MakeKey(int i, size_t size);

  // Builds a random value of the given length.
  fidl::Array<uint8_t> MakeValue(size_t size);

 private:
  std::independent_bits_engine<std::default_random_engine, CHAR_BIT, uint8_t>
      generator_;
};

}  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TEST_DATA_GENERATOR_H_
