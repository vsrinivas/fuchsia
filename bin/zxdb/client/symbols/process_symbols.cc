// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/process_symbols.h"

#include "garnet/bin/zxdb/client/symbols/llvm_util.h"
#include "garnet/bin/zxdb/client/symbols/symbol.h"
#include "garnet/bin/zxdb/client/symbols/system_symbols.h"
#include "llvm/DebugInfo/Symbolize/Symbolize.h"

namespace zxdb {

struct ProcessSymbols::ModuleRecord {
  uint64_t base = 0;
  std::string name;  // From the target OS.

  // Unstripped binary on the local system. Empty if unknown (we know about
  // this module in the target, but don't know where it is locally).
  std::string local_path;
};

ProcessSymbols::ProcessSymbols(SystemSymbols* system) : system_(system) {}

ProcessSymbols::~ProcessSymbols() = default;

std::string ProcessSymbols::AddModule(uint64_t base,
                                      const std::string& build_id,
                                      const std::string& module_name) {
  ModuleRecord record;
  record.base = base;
  record.name = module_name;
  record.local_path = system_->BuildIDToPath(build_id);

  modules_[base] = record;
  return record.local_path;
}

Symbol ProcessSymbols::SymbolAtAddress(uint64_t address) const {
  auto found = modules_.lower_bound(address);
  if (found == modules_.begin())
    return Symbol();  // No symbols or before the start.
  --found;  // We want the first one less than or equal to.
  const ModuleRecord& record = found->second;

  auto out = system_->symbolizer()->symbolizeCode(
      record.local_path, address - record.base);
  if (!out)
    return Symbol();
  return SymbolFromDILineInfo(out.get());
}

}  // namespace zxdb
