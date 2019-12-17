// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/data_generator.h"

#include <lib/fidl/cpp/clone.h>

#include <algorithm>
#include <functional>
#include <string>

#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/lib/rng/random.h"
#include "third_party/abseil-cpp/absl/strings/str_cat.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {
namespace {

constexpr absl::string_view kKeyIdSeparator = "-";

}  // namespace

DataGenerator::DataGenerator(Random* random) : generator_(random->NewBitGenerator<uint64_t>()) {}

DataGenerator::~DataGenerator() = default;

std::vector<uint8_t> DataGenerator::MakeKey(int i, size_t size) {
  std::string i_str = std::to_string(i);
  LEDGER_DCHECK(i_str.size() + kKeyIdSeparator.size() <= size);
  auto rand_bytes = MakeValue(size - i_str.size() - kKeyIdSeparator.size());

  return convert::ToArray(
      absl::StrCat(convert::ExtendedStringView(rand_bytes), kKeyIdSeparator, i_str));
}

PageId DataGenerator::MakePageId() {
  PageId value;
  std::generate(value.id.begin(), value.id.end(), std::ref(generator_));
  return value;
}

std::vector<uint8_t> DataGenerator::MakeValue(size_t size) {
  std::vector<uint8_t> data(size);
  std::generate(data.begin(), data.end(), std::ref(generator_));
  return data;
}

std::vector<std::vector<uint8_t>> DataGenerator::MakeKeys(size_t key_count, size_t key_size,
                                                          size_t unique_key_count) {
  LEDGER_DCHECK(unique_key_count <= key_count);
  std::vector<std::vector<uint8_t>> keys(key_count);
  for (size_t i = 0; i < unique_key_count; i++) {
    keys[i] = MakeKey(i, key_size);
  }
  for (size_t i = unique_key_count; i < key_count; i++) {
    keys[i] = keys[i - unique_key_count];
  }
  return keys;
}

size_t DataGenerator::GetKeyId(const std::vector<uint8_t>& key) {
  std::string key_str = convert::ToString(key);
  size_t split_index = key_str.find_last_of(convert::ToString(kKeyIdSeparator));
  LEDGER_CHECK(split_index != std::string::npos &&
               split_index + kKeyIdSeparator.size() < key_str.size());
  return std::stoul(key_str.substr(split_index + kKeyIdSeparator.size()));
}

}  // namespace ledger
