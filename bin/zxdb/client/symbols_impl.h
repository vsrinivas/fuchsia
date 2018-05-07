// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/symbols.h"
#include "garnet/public/lib/fxl/macros.h"

namespace debug_ipc {
class Frame;
struct Module;
struct StackFrame;
}

namespace zxdb {

class ProcessSymbols;
class SystemSymbolsProxy;

// Main client interface for querying process symbol information.
//
// ProcessSymbols runs on the background thread. This class provides a proxy
// to that thread to help avoid threading mistakes.
//
// See system_symbols_proxy.h for a diagram.
class SymbolsImpl : public Symbols {
 public:
  // The SystemSymbolsProxy must outlive this class.
  SymbolsImpl(Session* session, SystemSymbolsProxy* system_symbols);
  ~SymbolsImpl();

  // Adds the given module to the process. The callback will be executed with
  // the local path of the module if it is found, or the empty string if it is
  // not found.
  void AddModule(const debug_ipc::Module& module,
                 std::function<void(const std::string&)> callback);

  // Replaces all modules with the given list.
  void SetModules(const std::vector<debug_ipc::Module>& modules);

  // Symbols implementation.
  void ResolveAddress(uint64_t address,
                      std::function<void(Location)> callback) override;
  void ResolveAddresses(
      std::vector<uint64_t> addresses,
      std::function<void(std::vector<Location>)> callback) override;
  void GetModuleInfo(
      std::function<void(std::vector<ModuleSymbolRecord> records)> callback)
    override;

 private:
  SystemSymbolsProxy* system_proxy_;  // Non-owning.
  std::unique_ptr<ProcessSymbols> symbols_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SymbolsImpl);
};

}  // namespace zxdb
