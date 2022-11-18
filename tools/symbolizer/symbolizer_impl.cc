// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/symbolizer/symbolizer_impl.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>

#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/rapidjson.h>

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
#include "src/developer/debug/zxdb/common/file_util.h"
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

std::string FormatFrameIdAndAddress(uint64_t frame_id, uint64_t inline_index, uint64_t address) {
  // Frame number.
  std::string out = "   #" + std::to_string(frame_id);

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

SymbolizerImpl::SymbolizerImpl(Printer* printer, const CommandLineOptions& options,
                               AnalyticsSender sender)
    : printer_(printer), omit_module_lines_(options.omit_module_lines), sender_(std::move(sender)) {
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
    remote_symbol_lookup_enabled_ = true;
    loop_.Run();
  }

  // Check and prompt authentication message.
  auto symbol_servers = session_.system().GetSymbolServers();
  if (std::any_of(symbol_servers.begin(), symbol_servers.end(), [](zxdb::SymbolServer* server) {
        return server->state() == zxdb::SymbolServer::State::kAuth;
      })) {
    std::cerr << "WARN: missing authentication for symbol servers. You might want to run "
                 "`ffx debug symbolize --auth`.\n";
  }

  if (options.dumpfile_output) {
    dumpfile_output_ = options.dumpfile_output.value();
    dumpfile_document_.SetArray();
    ResetDumpfileCurrentObject();
  }
}

SymbolizerImpl::~SymbolizerImpl() {
  loop_.Cleanup();

  // Support for dumpfile
  if (!dumpfile_output_.empty()) {
    std::ofstream ofs(dumpfile_output_);
    rapidjson::OStreamWrapper osw(ofs);
    rapidjson::PrettyWriter<rapidjson::OStreamWrapper> writer(osw);
    writer.SetIndent(' ', 2);
    dumpfile_document_.Accept(writer);
  }
}

void SymbolizerImpl::Reset(bool symbolizing_dart) {
  symbolizing_dart_ = symbolizing_dart;

  modules_.clear();
  address_to_module_id_.clear();
  if (target_->GetState() == zxdb::Target::State::kRunning) {
    // OnProcessExiting() will destroy the Process, ProcessSymbols.
    // Retain references to loaded TargetSymbols in |previous_modules_| so that they can be
    // potentially reused for the subsequent stack trace.
    previous_modules_ = target_->GetProcess()->GetSymbols()->target_symbols()->TakeModules();
    target_->OnProcessExiting(/*return_code=*/0, /*timestamp=*/0);
  }

  if (analytics_builder_.valid()) {
    analytics_builder_.SetRemoteSymbolLookupEnabledBit(remote_symbol_lookup_enabled_);
    if (sender_) {
      sender_(analytics_builder_.build());
    }
    analytics_builder_ = {};
  }

  // Support for dumpfile
  if (!dumpfile_output_.empty()) {
    ResetDumpfileCurrentObject();
  }
}

void SymbolizerImpl::Module(uint64_t id, std::string_view name, std::string_view build_id) {
  modules_[id].name = name;
  modules_[id].build_id = build_id;

  // Support for dumpfile
  if (!dumpfile_output_.empty()) {
    rapidjson::Value module(rapidjson::kObjectType);
    module.AddMember("name", ToJSONString(name), dumpfile_document_.GetAllocator());
    module.AddMember("build", ToJSONString(build_id), dumpfile_document_.GetAllocator());
    module.AddMember("id", id, dumpfile_document_.GetAllocator());
    dumpfile_current_object_["modules"].PushBack(module, dumpfile_document_.GetAllocator());
  }
}

void SymbolizerImpl::MMap(uint64_t address, uint64_t size, uint64_t module_id,
                          std::string_view flags, uint64_t module_offset) {
  if (modules_.find(module_id) == modules_.end()) {
    analytics_builder_.SetAtLeastOneInvalidInput();
    printer_->OutputWithContext("symbolizer: Invalid module id.");
    return;
  }

  ModuleInfo& module = modules_[module_id];
  uint64_t base = address - module_offset;

  if (address < module_offset) {
    // Negative load address. This happens for zircon on x64.
    if (module.printed) {
      if (module.base != 0 || module.negative_base != module_offset - address) {
        analytics_builder_.SetAtLeastOneInvalidInput();
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
        analytics_builder_.SetAtLeastOneInvalidInput();
        printer_->OutputWithContext("symbolizer: Inconsistent base address.");
      }
    } else {
      module.base = base;
    }
    if (module.size < size + module_offset) {
      module.size = size + module_offset;
    }
  }

  if (!omit_module_lines_ && !symbolizing_dart_ && !module.printed) {
    printer_->OutputWithContext(
        fxl::StringPrintf("[[[ELF module #0x%" PRIx64 " \"%s\" BuildID=%s 0x%" PRIx64 "]]]",
                          module_id, module.name.c_str(), module.build_id.c_str(), base));
    module.printed = true;
  }

  // Support for dumpfile
  if (!dumpfile_output_.empty()) {
    rapidjson::Value segment(rapidjson::kObjectType);
    segment.AddMember("mod", module_id, dumpfile_document_.GetAllocator());
    segment.AddMember("vaddr", address, dumpfile_document_.GetAllocator());
    segment.AddMember("size", size, dumpfile_document_.GetAllocator());
    segment.AddMember("flags", ToJSONString(flags), dumpfile_document_.GetAllocator());
    segment.AddMember("mod_rel_addr", module_offset, dumpfile_document_.GetAllocator());
    dumpfile_current_object_["segments"].PushBack(segment, dumpfile_document_.GetAllocator());
  }
}

void SymbolizerImpl::Backtrace(uint64_t frame_id, uint64_t address, AddressType type,
                               std::string_view message) {
  InitProcess();
  analytics_builder_.IncreaseNumberOfFrames();

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
        FormatFrameIdAndAddress(frame_id, 0, address) + " is not covered by any module";
    if (!message.empty()) {
      out += " " + std::string(message);
    }
    analytics_builder_.IncreaseNumberOfFramesInvalid();
    analytics_builder_.TotalTimerStop();
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

  // All modules for this stack trace have been loaded by this point, so we can discard retained
  // data from previously handled stack traces (if any).
  previous_modules_.clear();

  bool symbolized = false;
  for (size_t i = 0; i < stack.size(); i++) {
    std::string out = FormatFrameIdAndAddress(frame_id, stack.size() - i - 1, address);

    out += " in";

    // Function name.
    const zxdb::Location location = stack[i]->GetLocation();
    if (location.symbol().is_valid()) {
      symbolized = true;
      auto symbol = location.symbol().Get();
      auto function = symbol->As<zxdb::Function>();
      if (function && !symbolizing_dart_) {
        out += " " + zxdb::FormatFunctionName(function, {}).AsString();
      } else {
        out += " " + symbol->GetFullName();
      }
    }

    // FileLine info.
    if (location.file_line().is_valid()) {
      symbolized = true;
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

  // One physical frame could be symbolized to multiple inlined frames. We're only counting the
  // number of physical frames symbolized.
  if (symbolized) {
    analytics_builder_.IncreaseNumberOfFramesSymbolized();
  }
  analytics_builder_.TotalTimerStop();
}

void SymbolizerImpl::DumpFile(std::string_view type, std::string_view name) {
  if (!dumpfile_output_.empty()) {
    dumpfile_current_object_.AddMember("type", ToJSONString(type),
                                       dumpfile_document_.GetAllocator());
    dumpfile_current_object_.AddMember("name", ToJSONString(name),
                                       dumpfile_document_.GetAllocator());
    dumpfile_document_.PushBack(dumpfile_current_object_, dumpfile_document_.GetAllocator());
    ResetDumpfileCurrentObject();
  }
}

void SymbolizerImpl::OnDownloadsStarted() {
  if (remote_symbol_lookup_enabled_) {
    analytics_builder_.DownloadTimerStart();
  }
  is_downloading_ = true;
}

void SymbolizerImpl::OnDownloadsStopped(size_t num_succeeded, size_t num_failed) {
  // Even if no symbol server is configured, this function could still be invoked but all
  // downloadings will be failed.
  if (remote_symbol_lookup_enabled_) {
    analytics_builder_.SetNumberOfModulesWithDownloadedSymbols(num_succeeded);
    analytics_builder_.SetNumberOfModulesWithDownloadingFailure(num_failed);
    analytics_builder_.DownloadTimerStop();
  }
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

  analytics_builder_.TotalTimerStart();

  session_.DispatchNotifyProcessStarting({});
  session_.DispatchNotifyThreadStarting({});

  std::vector<debug_ipc::Module> modules;
  modules.reserve(modules_.size());
  for (const auto& pair : modules_) {
    modules.push_back({pair.second.name, pair.second.base, 0, pair.second.build_id});
    address_to_module_id_[pair.second.base] = pair.first;
  }
  target_->GetProcess()->GetSymbols()->SetModules(modules);

  // Collect module info for analytics.
  size_t num_modules_with_cached_symbols = 0;
  size_t num_modules_with_local_symbols = 0;
  auto cache_dir = session_.system().GetSymbols()->build_id_index().GetCacheDir();
  // GetModuleSymbols() will only return loaded modules.
  for (auto module_symbol : target_->GetSymbols()->GetModuleSymbols()) {
    if (!cache_dir.empty() &&
        zxdb::PathStartsWith(module_symbol->GetStatus().symbol_file, cache_dir)) {
      num_modules_with_cached_symbols++;
    } else {
      num_modules_with_local_symbols++;
    }
  }
  analytics_builder_.SetNumberOfModules(modules_.size());
  analytics_builder_.SetNumberOfModulesWithCachedSymbols(num_modules_with_cached_symbols);
  analytics_builder_.SetNumberOfModulesWithLocalSymbols(num_modules_with_local_symbols);

  // Wait until downloading completes.
  if (is_downloading_) {
    loop_.Run();
  }
}

void SymbolizerImpl::ResetDumpfileCurrentObject() {
  dumpfile_current_object_.SetObject();
  dumpfile_current_object_.AddMember("modules", rapidjson::kArrayType,
                                     dumpfile_document_.GetAllocator());
  dumpfile_current_object_.AddMember("segments", rapidjson::kArrayType,
                                     dumpfile_document_.GetAllocator());
}

rapidjson::Value SymbolizerImpl::ToJSONString(std::string_view str) {
  rapidjson::Value string;
  string.SetString(str.data(), static_cast<uint32_t>(str.size()),
                   dumpfile_document_.GetAllocator());
  return string;
}

}  // namespace symbolizer
