// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <string>

namespace zxdb {

struct ModuleSymbolStatus {
  // Name of the executable or shared library on the system.
  std::string name;

  // Build ID extracted from file.
  std::string build_id;

  // Load address.
  uint64_t base = 0;

  // True if the symbols were successfully loaded.
  bool symbols_loaded = false;

  size_t functions_indexed = 0;
  size_t files_indexed = 0;

  // Local file name with the symbols if the symbols were loaded.
  std::string symbol_file;
};

}  // namespace zxdb
