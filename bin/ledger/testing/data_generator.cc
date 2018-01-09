// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/data_generator.h"

#include <algorithm>
#include <functional>
#include <string>

#include "lib/fxl/logging.h"
#include "lib/fxl/random/rand.h"
#include "lib/fxl/strings/concatenate.h"
#include "peridot/lib/convert/convert.h"

namespace {
constexpr size_t kPageIdSize = 16;
}

namespace test {

DataGenerator::DataGenerator() : generator_(fxl::RandUint64()) {}

DataGenerator::DataGenerator(uint64_t seed) : generator_(seed) {}

DataGenerator::~DataGenerator() {}

fidl::Array<uint8_t> DataGenerator::MakeKey(int i, size_t size) {
  std::string i_str = std::to_string(i);
  FXL_DCHECK(i_str.size() + 1 <= size);
  auto rand_bytes = MakeValue(size - i_str.size() - 1);

  return convert::ToArray(
      fxl::Concatenate({i_str, "-", convert::ExtendedStringView(rand_bytes)}));
}

fidl::Array<uint8_t> DataGenerator::MakePageId() {
  return MakeValue(kPageIdSize);
}

fidl::Array<uint8_t> DataGenerator::MakeValue(size_t size) {
  auto data = fidl::Array<uint8_t>::New(size);
  std::generate(data.begin(), data.end(), std::ref(generator_));
  return data;
}

std::vector<fidl::Array<uint8_t>> DataGenerator::MakeKeys(
    size_t key_count,
    size_t key_size,
    size_t unique_key_count) {
  FXL_DCHECK(unique_key_count <= key_count);
  std::vector<fidl::Array<uint8_t>> keys(key_count);
  for (size_t i = 0; i < unique_key_count; i++) {
    keys[i] = MakeKey(i, key_size);
  }
  for (size_t i = unique_key_count; i < key_count; i++) {
    keys[i] = keys[i - unique_key_count].Clone();
  }
  return keys;
}

}  // namespace test
