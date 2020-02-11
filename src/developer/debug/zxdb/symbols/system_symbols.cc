// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/system_symbols.h"

#include "src/developer/debug/zxdb/common/file_util.h"
#include "src/developer/debug/zxdb/common/host_util.h"
#include "src/developer/debug/zxdb/common/ref_ptr_to.h"
#include "src/developer/debug/zxdb/symbols/dwarf_binary_impl.h"
#include "src/developer/debug/zxdb/symbols/module_symbols_impl.h"
#include "src/lib/elflib/elflib.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

SystemSymbols::SystemSymbols(DownloadHandler* download_handler)
    : download_handler_(download_handler), weak_factory_(this) {}

SystemSymbols::~SystemSymbols() = default;

void SystemSymbols::InjectModuleForTesting(const std::string& build_id, ModuleSymbols* module) {
  SaveModule(build_id, module);
}

Err SystemSymbols::GetModule(const std::string& build_id, fxl::RefPtr<ModuleSymbols>* module,
                             SystemSymbols::DownloadType download_type) {
  *module = fxl::RefPtr<ModuleSymbols>();

  auto found_existing = modules_.find(build_id);
  if (found_existing != modules_.end()) {
    *module = RefPtrTo(found_existing->second);
    return Err();
  }

  std::string file_name = build_id_index_.FileForBuildID(build_id, DebugSymbolFileType::kDebugInfo);
  std::string binary_file_name =
      build_id_index_.FileForBuildID(build_id, DebugSymbolFileType::kBinary);

  if (file_name.empty() && download_type == SystemSymbols::DownloadType::kSymbols &&
      download_handler_) {
    download_handler_->RequestDownload(build_id, DebugSymbolFileType::kDebugInfo, false);
  }

  if (auto debug = elflib::ElfLib::Create(file_name)) {
    if (!debug->ProbeHasProgramBits() && binary_file_name.empty() &&
        download_type == SystemSymbols::DownloadType::kBinary && download_handler_) {
      // File doesn't exist or has no symbols, schedule a download.
      download_handler_->RequestDownload(build_id, DebugSymbolFileType::kBinary, false);
    }
  }

  if (file_name.empty())
    return Err();  // No symbols synchronously available.

  auto binary = std::make_unique<DwarfBinaryImpl>(file_name, binary_file_name, build_id);
  if (Err err = binary->Load(); err.has_error())
    return err;  // Symbols corrupt.

  *module = fxl::MakeRefCounted<ModuleSymbolsImpl>(std::move(binary));

  SaveModule(build_id, module->get());  // Save in cache for future use.
  return Err();
}

void SystemSymbols::SaveModule(const std::string& build_id, ModuleSymbols* module) {
  // Can't save a module that already exists.
  FXL_DCHECK(modules_.find(build_id) == modules_.end());

  module->set_deletion_cb(
      [weak_system = weak_factory_.GetWeakPtr(), build_id](ModuleSymbols* module) {
        if (!weak_system)
          return;
        SystemSymbols* system = weak_system.get();

        auto found = system->modules_.find(build_id);
        if (found == system->modules_.end()) {
          FXL_NOTREACHED();  // Should be found if we registered.
          return;
        }

        FXL_DCHECK(module == found->second);  // Mapping should match.
        system->modules_.erase(found);
      });
  modules_[build_id] = module;
}

}  // namespace zxdb
