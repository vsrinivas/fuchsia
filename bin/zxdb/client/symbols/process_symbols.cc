// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/process_symbols.h"

#include "garnet/bin/zxdb/client/symbols/llvm_util.h"
#include "garnet/bin/zxdb/client/symbols/module_records.h"
#include "garnet/bin/zxdb/client/symbols/symbol.h"
#include "garnet/bin/zxdb/client/symbols/system_symbols.h"
#include "llvm/DebugInfo/Symbolize/Symbolize.h"

namespace zxdb {

// IMPORTANT: This class must not dereference the SystemSymbols pointer in
// the constructor. It may be created on different thread than the
// SystemSymbols is running on. This allows future calls to this class to be
// posted to the symbol thread from a separate thread.
ProcessSymbols::ProcessSymbols(SystemSymbols* system) : system_(system) {
  FXL_DCHECK(system_);
}

ProcessSymbols::~ProcessSymbols() = default;

std::string ProcessSymbols::AddModule(const ModuleLoadInfo& info) {
  ModuleSymbolRecord record(info);
  record.local_path = system_->BuildIDToPath(info.build_id);

  modules_[info.base] = record;
  return record.local_path;
}

void ProcessSymbols::SetModules(const std::vector<ModuleLoadInfo>& info) {
  modules_.clear();
  for (const auto& module : info)
    AddModule(module);
}

Location ProcessSymbols::ResolveAddress(uint64_t address) const {
  Location result(address);

  auto found = modules_.lower_bound(address);
  if (found == modules_.begin())
    return result;  // No symbols or before the start.
  --found;  // We want the first one less than or equal to.
  const ModuleSymbolRecord& record = found->second;

  auto out = system_->symbolizer()->symbolizeCode(
      record.local_path, address - record.base);
  if (!out) {
    // Currently it's interesting to see what kinds of errors we get from
    // LLVM. When we don't want it spewing, the printf should be removed
    // and replaced with:
    //   llvm::consumeError(out.takeError());
    auto err_str = llvm::toString(out.takeError());
    fprintf(stderr, "Symbol error: %s\n", err_str.c_str());
    return result;
  }

  result.set_symbol(SymbolFromDILineInfo(out.get()));
  return result;
}

}  // namespace zxdb
