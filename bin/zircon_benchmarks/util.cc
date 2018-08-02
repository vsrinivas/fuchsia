// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"
#include <lib/fxl/strings/string_printf.h>
#include <random>

namespace util {

std::vector<std::string> MakeDeterministicNamesList(int length) {
  std::vector<std::string> ret;
  for (int i = 0; i < length; ++i) {
    ret.emplace_back(fxl::StringPrintf("%07d", i));
  }

  std::shuffle(ret.begin(), ret.end(), std::default_random_engine(0x2128847));

  return ret;
}

}  // namespace util
