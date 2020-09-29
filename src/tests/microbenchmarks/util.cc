// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <random>

#include "src/lib/fxl/strings/string_printf.h"

namespace util {

std::vector<std::string> MakeDeterministicNamesList(size_t length) {
  std::vector<std::string> ret;
  for (size_t i = 0; i < length; ++i) {
    ret.emplace_back(fxl::StringPrintf("%07zu", i));
  }

  std::shuffle(ret.begin(), ret.end(), std::default_random_engine(0x2128847));

  return ret;
}

}  // namespace util
