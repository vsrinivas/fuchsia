// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <string>

// This file contains structures used for symbol module handling.

namespace zxdb {

// Used as inputs when a module is loaded.
struct ModuleLoadInfo {
  uint64_t base;
  std::string build_id;
  std::string module_name;  // Name on target system (no path).
};

// Tracks the symbol info for a given module.
struct ModuleSymbolRecord : public ModuleLoadInfo {
  ModuleSymbolRecord() = default;
  ModuleSymbolRecord(const ModuleLoadInfo& info) : ModuleLoadInfo(info) {}

  // Unstripped binary on the local system. Empty if unknown (we know about
  // this module in the target, but don't know where it is locally).
  std::string local_path;
};

}  // namespace zxdb
