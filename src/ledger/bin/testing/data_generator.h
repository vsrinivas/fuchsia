// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTING_DATA_GENERATOR_H_
#define SRC_LEDGER_BIN_TESTING_DATA_GENERATOR_H_

#include <lib/fidl/cpp/vector.h>

#include <climits>
#include <random>
#include <vector>

#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/lib/rng/random.h"

namespace ledger {

class DataGenerator {
 public:
  explicit DataGenerator(Random* random);

  ~DataGenerator();

  // Builds a key of the given |size| as "<random data>-<i>". The id (|i|) of
  // the result can be retrieved by calling |GetKeyId|.
  std::vector<uint8_t> MakeKey(int i, size_t size);

  // Builds a random value that can be used as a page id.
  PageId MakePageId();

  // Builds a random value of the given length.
  std::vector<uint8_t> MakeValue(size_t size);

  // Builds a vector of length |key_count| containing keys of size |key_size|,
  // |unique_key_count| of which are unique.
  std::vector<std::vector<uint8_t>> MakeKeys(size_t key_count, size_t key_size,
                                             size_t unique_key_count);

  // Returns the id of |key|. |key| is assumed to have been created by this
  // DataGenerator, using either |MakeKey| or |MakeKeys|.
  size_t GetKeyId(const std::vector<uint8_t>& key);

 private:
  std::independent_bits_engine<Random::BitGenerator<uint64_t>, CHAR_BIT, uint8_t> generator_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTING_DATA_GENERATOR_H_
