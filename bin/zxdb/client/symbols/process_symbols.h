// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>

#include "garnet/bin/zxdb/client/symbols/location.h"
#include "garnet/public/lib/fxl/macros.h"
#include "garnet/public/lib/fxl/observer_list.h"

namespace zxdb {

struct ModuleLoadInfo;
struct ModuleSymbolRecord;
class SystemSymbols;

// Tracks the modules loaded in a process and resolves symbols based on this.
class ProcessSymbols {
 public:
  // The SystemSymbols must outlive this object.
  explicit ProcessSymbols(SystemSymbols* system);
  ~ProcessSymbols();

  const std::map<uint64_t, ModuleSymbolRecord>& modules() const {
    return modules_;
  }

  // Returns the local path of the module if it was found, or the empty string
  // if not. If not found, the module will have no symbols.
  std::string AddModule(const ModuleLoadInfo& info);

  // Replaces all current modules with the given updated list.
  void SetModules(const std::vector<ModuleLoadInfo>& info);

  // If the address can't be resolved, the Symbol in the address will be
  // !valid().
  Location ResolveAddress(uint64_t address) const;

 private:
  SystemSymbols* system_;

  std::map<uint64_t, ModuleSymbolRecord> modules_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ProcessSymbols);
};

}  // namespace zxdb
