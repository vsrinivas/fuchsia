// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_BENCHMARK_LIB_CONVERT_H_
#define APPS_LEDGER_BENCHMARK_LIB_CONVERT_H_

#include <string>

#include "lib/fidl/cpp/bindings/array.h"

namespace benchmark {

std::string ToString(fidl::Array<uint8_t>& data);

fidl::Array<uint8_t> ToArray(const std::string& value);

}  // namespace benchmark

#endif  // APPS_LEDGER_BENCHMARK_LIB_CONVERT_H_
