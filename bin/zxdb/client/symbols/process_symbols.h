// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>

#include "garnet/bin/zxdb/client/symbols/symbol.h"
#include "garnet/public/lib/fxl/macros.h"
#include "garnet/public/lib/fxl/observer_list.h"

namespace zxdb {

class SystemSymbols;

// Tracks the modules loaded in a process and resolves symbols based on this.
class ProcessSymbols {
 public:
  // The SystemSymbols must outlive this object.
  explicit ProcessSymbols(SystemSymbols* system);
  ~ProcessSymbols();

  // Returns the local path of the module if it was found, or the empty string
  // if not. If not found, the module will have no symbols.
  std::string AddModule(uint64_t base,
                        const std::string& build_id,
                        const std::string& module_name);

  // Returns an !valid() Symbol if nothing can be resolved.
  Symbol SymbolAtAddress(uint64_t address) const;

 private:
  struct ModuleRecord;

  SystemSymbols* system_;

  std::map<uint64_t, ModuleRecord> modules_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ProcessSymbols);
};

}  // namespace zxdb
