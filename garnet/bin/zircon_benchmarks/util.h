// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_ZIRCON_BENCHMARKS_UTIL_H_
#define GARNET_BIN_ZIRCON_BENCHMARKS_UTIL_H_

#include <vector>

namespace util {

std::vector<std::string> MakeDeterministicNamesList(int length);

}  // namespace util

#endif  // GARNET_BIN_ZIRCON_BENCHMARKS_UTIL_H_
