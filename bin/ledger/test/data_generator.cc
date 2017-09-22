// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/test/data_generator.h"

#include <algorithm>
#include <functional>
#include <string>

#include "peridot/bin/ledger/convert/convert.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/random/rand.h"
#include "lib/fxl/strings/concatenate.h"

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

fidl::Array<uint8_t> DataGenerator::MakeValue(size_t size) {
  auto data = fidl::Array<uint8_t>::New(size);
  std::generate(data.begin(), data.end(), std::ref(generator_));
  return data;
}

}  // namespace test
