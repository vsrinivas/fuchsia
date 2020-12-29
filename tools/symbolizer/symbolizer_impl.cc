// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/symbolizer/symbolizer_impl.h"

#include <string>

#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/zxdb/client/client_object.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/stack.h"
#include "src/developer/debug/zxdb/client/symbol_server.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/console/format_name.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/loaded_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"
#include "src/developer/debug/zxdb/symbols/system_symbols.h"
#include "src/developer/debug/zxdb/symbols/target_symbols.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace symbolizer {

namespace {

void SetupCommandLineOptions(const CommandLineOptions& options, zxdb::MapSettingStore& settings) {
  using Settings = zxdb::ClientSettings;

  if (options.symbol_cache) {
    settings.SetString(Settings::System::kSymbolCache, *options.symbol_cache);
  }

  if (!options.symbol_index_files.empty()) {
    settings.SetList(Settings::System::kSymbolIndexFiles, options.symbol_index_files);
  }

  if (!options.symbol_servers.empty()) {
    settings.SetList(Settings::System::kSymbolServers, options.symbol_servers);
  }

  if (!options.symbol_paths.empty()) {
    settings.SetList(Settings::System::kSymbolPaths, options.symbol_paths);
  }

  if (!options.build_id_dirs.empty()) {
    settings.SetList(Settings::System::kBuildIdDirs, options.build_id_dirs);
  }

  if (!options.ids_txts.empty()) {
    settings.SetList(Settings::System::kIdsTxts, options.ids_txts);
  }

  if (!options.build_dirs.empty()) {
    settings.SetList(Settings::Target::kBuildDirs, options.build_dirs);
  }
}

std::string FormatFrameIndexAndAddress(int frame_index, int inline_index, uint64_t address) {
  // Frame number.
  std::string out = "   #" + std::to_string(frame_index);

  // Append a sequence for inline frames, i.e. not the last frame.
  if (inline_index) {
    out += "." + std::to_string(inline_index);
  }

  // Pad to 9.
  constexpr int index_width = 9;
  if (out.length() < index_width) {
    out += std::string(index_width - out.length(), ' ');
  }

  // Print the absolute address first.
  out += fxl::StringPrintf("0x%016" PRIx64, address);

  return out;
}

}  // namespace

SymbolizerImpl::SymbolizerImpl(Printer* printer, const CommandLineOptions& options)
    : printer_(printer), omit_module_lines_(options.omit_module_lines) {
  // Hook observers.
  session_.system().AddObserver(this);
  session_.AddDownloadObserver(this);

  // Disable indexing on ModuleSymbols to accelerate the loading time.
  session_.system().GetSymbols()->set_create_index(false);
  target_ = session_.system().GetTargets()[0];

  loop_.Init(nullptr);
  // Setting symbol servers will trigger an asynchronous network request.
  SetupCommandLineOptions(options, session_.system().settings());
  if (waiting_auth_) {
    loop_.Run();
  }
}

SymbolizerImpl::~SymbolizerImpl() { loop_.Cleanup(); }

void SymbolizerImpl::Reset() {
  modules_.clear();
  address_to_module_id_.clear();
  if (target_->GetState() == zxdb::Target::State::kRunning) {
    // OnProcessExiting() will destroy the Process, ProcessSymbols but we still keep references
    // to ModuleSymbols in TargetSymbols.
    //
    // We should be able to use target_->GetSymbols(). However, it returns a const pointer.
    target_->GetProcess()->GetSymbols()->target_symbols()->RemoveAllModules();
    target_->OnProcessExiting(0);
  }
}

void SymbolizerImpl::Module(uint64_t id, std::string_view name, std::string_view build_id) {
  modules_[id].name = name;
  modules_[id].build_id = build_id;
}

void SymbolizerImpl::MMap(uint64_t address, uint64_t size, uint64_t module_id,
                          uint64_t module_offset) {
  if (modules_.find(module_id) == modules_.end()) {
    printer_->OutputWithContext("symbolizer: Invalid module id.");
    return;
  }

  ModuleInfo& module = modules_[module_id];
  uint64_t base = address - module_offset;

  if (address < module_offset) {
    // Negative load address. This happens for zircon on x64.
    if (module.printed) {
      if (module.base != 0 || module.negative_base != module_offset - address) {
        printer_->OutputWithContext("symbolizer: Inconsistent base address.");
      }
    } else {
      base = address;  // for printing only
      module.base = 0;
      module.negative_base = module_offset - address;
    }
    if (module.size < address + size) {
      module.size = address + size;
    }
  } else {
    if (module.printed) {
      if (module.base != base) {
        printer_->OutputWithContext("symbolizer: Inconsistent base address.");
      }
    } else {
      module.base = base;
    }
    if (module.size < size + module_offset) {
      module.size = size + module_offset;
    }
  }

  if (!omit_module_lines_ && !module.printed) {
    printer_->OutputWithContext(
        fxl::StringPrintf("[[[ELF module #0x%" PRIx64 " \"%s\" BuildID=%s 0x%" PRIx64 "]]]",
                          module_id, module.name.c_str(), module.build_id.c_str(), base));
    module.printed = true;
  }
}

void SymbolizerImpl::Backtrace(int frame_index, uint64_t address, AddressType type,
                               std::string_view message) {
  InitProcess();

  // Find the module to see if the stack might be corrupt.
  const ModuleInfo* module = nullptr;
  if (auto next = address_to_module_id_.upper_bound(address);
      next != address_to_module_id_.begin()) {
    next--;
    const auto& module_id = next->second;
    const auto& prev = modules_[module_id];
    if (address - prev.base <= prev.size) {
      module = &prev;
    }
  }

  if (!module) {
    std::string out =
        FormatFrameIndexAndAddress(frame_index, 0, address) + " is not covered by any module";
    if (!message.empty()) {
      out += " " + std::string(message);
    }
    return printer_->OutputWithContext(out);
  }

  uint64_t call_address = address;

  if (module->negative_base) {
    call_address += module->negative_base;
  }
  // Substracts 1 from the address if it's a return address or unknown. It shouldn't be an issue
  // for unknown addresses as most instructions are more than 1 byte.
  if (type != AddressType::kProgramCounter) {
    call_address -= 1;
  }

  debug_ipc::StackFrame frame{call_address, 0};
  zxdb::Stack& stack = target_->GetProcess()->GetThreads()[0]->GetStack();
  stack.SetFrames(debug_ipc::ThreadRecord::StackAmount::kFull, {frame});

  for (size_t i = 0; i < stack.size(); i++) {
    std::string out = FormatFrameIndexAndAddress(frame_index, stack.size() - i - 1, address);

    out += " in";

    // Function name.
    const zxdb::Location location = stack[i]->GetLocation();
    if (location.symbol().is_valid()) {
      auto symbol = location.symbol().Get();
      if (auto function = symbol->AsFunction()) {
        out += " " + zxdb::FormatFunctionName(function, {}).AsString();
      } else {
        out += " " + symbol->GetFullName();
      }
    }

    // FileLine info.
    if (location.file_line().is_valid()) {
      out += " " + location.file_line().file() + ":" + std::to_string(location.file_line().line());
    }

    // Module offset.
    out += fxl::StringPrintf(" <%s>+0x%" PRIx64, module->name.c_str(),
                             address - module->base + module->negative_base);

    // Extra message.
    if (!message.empty()) {
      out += " " + std::string(message);
    }

    printer_->OutputWithContext(out);
  }
}

void SymbolizerImpl::OnDownloadsStarted() { is_downloading_ = true; }

void SymbolizerImpl::OnDownloadsStopped(size_t num_succeeded, size_t num_failed) {
  is_downloading_ = false;
  loop_.QuitNow();
}

void SymbolizerImpl::DidCreateSymbolServer(zxdb::SymbolServer* server) {
  if (server->state() == zxdb::SymbolServer::State::kInitializing ||
      server->state() == zxdb::SymbolServer::State::kBusy) {
    waiting_auth_ = true;
  }
}

void SymbolizerImpl::OnSymbolServerStatusChanged(zxdb::SymbolServer* unused_server) {
  if (!waiting_auth_) {
    return;
  }

  for (auto& server : session_.system().GetSymbolServers()) {
    if (server->state() == zxdb::SymbolServer::State::kInitializing ||
        server->state() == zxdb::SymbolServer::State::kBusy) {
      return;
    }
  }

  waiting_auth_ = false;
  loop_.QuitNow();
}

void SymbolizerImpl::InitProcess() {
  // Only initialize once, i.e. on the first frame of the backtrace.
  // DispatchProcessStarting will set the state to kRunning.
  if (target_->GetState() != zxdb::Target::State::kNone) {
    return;
  }

  session_.DispatchProcessStarting({});
  session_.DispatchNotifyThreadStarting({});

  std::vector<debug_ipc::Module> modules;
  modules.reserve(modules_.size());
  for (const auto& pair : modules_) {
    modules.push_back({pair.second.name, pair.second.base, 0, pair.second.build_id});
    address_to_module_id_[pair.second.base] = pair.first;
  }
  target_->GetProcess()->GetSymbols()->SetModules(modules);

  // Wait until downloading completes.
  if (is_downloading_) {
    loop_.Run();
  }
}

}  // namespace symbolizer
