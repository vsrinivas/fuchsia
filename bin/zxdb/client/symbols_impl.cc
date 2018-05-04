// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols_impl.h"

#include "garnet/bin/zxdb/client/symbols/module_records.h"
#include "garnet/bin/zxdb/client/symbols/process_symbols.h"
#include "garnet/bin/zxdb/client/system_symbols_proxy.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"
#include "garnet/lib/debug_ipc/records.h"

namespace zxdb {

namespace {

ModuleLoadInfo ModuleLoadInfoFromDebugIPC(const debug_ipc::Module& in) {
  ModuleLoadInfo out;
  out.base = in.base;
  out.build_id = in.build_id;
  out.module_name = in.name;
  return out;
}

}  // namespace

// Constructs the ProcessSymbols class with the SystemSymbols pointer. As long
// as it doesn't dereference the pointer (which it doesn't), this will be
// save even though this is not the symbol thread.
SymbolsImpl::SymbolsImpl(Session* session, SystemSymbolsProxy* system_symbols)
    : Symbols(session),
      system_proxy_(system_symbols),
      symbols_(
          std::make_unique<ProcessSymbols>(system_proxy_->symbols_.get())) {}

SymbolsImpl::~SymbolsImpl() {
  // Delete ProcessSymbols on the symbol thread.
  system_proxy_->symbol_loop()->PostTask([symbols = symbols_.release()]() {
    delete symbols;
  });
}

void SymbolsImpl::AddModule(
    const debug_ipc::Module& module,
    std::function<void(const std::string&)> callback) {
  ModuleLoadInfo info = ModuleLoadInfoFromDebugIPC(module);

  system_proxy_->symbol_loop()->PostTask(
      [ symbols = symbols_.get(), info,
          main_loop = system_proxy_->main_loop(), callback](){
        std::string local_path = symbols->AddModule(info);
        main_loop->PostTask([ callback, local_path ](){ callback(local_path); });
      });
}

void SymbolsImpl::SetModules(const std::vector<debug_ipc::Module>& modules) {
  std::vector<ModuleLoadInfo> info;
  for (const auto& module : modules)
    info.push_back(ModuleLoadInfoFromDebugIPC(module));

  system_proxy_->symbol_loop()->PostTask([ symbols = symbols_.get(), info ]() {
    symbols->SetModules(info);
  });
}

void SymbolsImpl::SymbolAtAddress(uint64_t address,
                                  std::function<void(Symbol)> callback) {
  system_proxy_->symbol_loop()->PostTask(
      [ symbols = symbols_.get(), address, callback,
          main_loop = system_proxy_->main_loop() ](){
        Symbol result = symbols->SymbolAtAddress(address);
        main_loop->PostTask([ callback, result ](){ callback(result); });
      });
}

void SymbolsImpl::GetModuleInfo(
    std::function<void(std::vector<ModuleSymbolRecord> records)> callback) {
  system_proxy_->symbol_loop()->PostTask(
      [ symbols = symbols_.get(), main_loop = system_proxy_->main_loop(),
          callback](){
        std::vector<ModuleSymbolRecord> records;
        for (const auto& record : symbols->modules())
          records.push_back(record.second);

        main_loop->PostTask([ callback, records = std::move(records) ](){
          callback(std::move(records));
        });
      });
}

}  // namespace zxdb
