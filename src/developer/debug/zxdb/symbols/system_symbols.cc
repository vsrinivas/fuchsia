// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/system_symbols.h"

#include "src/developer/debug/zxdb/common/file_util.h"
#include "src/developer/debug/zxdb/common/host_util.h"
#include "src/developer/debug/zxdb/symbols/module_symbols_impl.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// TODO(brettw) this is hardcoded and will only work in a full local build.
// We will need a more flexible way to do handle this, and also a way to
// explicitly specify a location for the mapping file.
std::string GetBuildDir() {
  // Expect the debugger to be in "<build>/host_x64/zxdb" and the build dir
  // to be one directory up.
  std::string path = GetSelfPath();
  if (path.empty())
    return path;

  // Trim off the last two slash-separated components ("host_x64/zxdb").
  size_t last_slash = path.rfind('/');
  if (last_slash != std::string::npos) {
    path.resize(last_slash);
    last_slash = path.rfind('/');
    if (last_slash != std::string::npos)
      path.resize(last_slash + 1);  // + 1 means keep the last slash.
  }
  return path;
}

}  // namespace

// SystemSymbols::ModuleRef ----------------------------------------------------

SystemSymbols::ModuleRef::ModuleRef(
    SystemSymbols* system_symbols,
    std::unique_ptr<ModuleSymbols> module_symbols)
    : system_symbols_(system_symbols),
      module_symbols_(std::move(module_symbols)) {}

SystemSymbols::ModuleRef::~ModuleRef() {
  if (system_symbols_)
    system_symbols_->WillDeleteModule(this);
}

void SystemSymbols::ModuleRef::SystemSymbolsDeleting() {
  system_symbols_ = nullptr;
}

// SystemSymbols ---------------------------------------------------------------

SystemSymbols::SystemSymbols(DownloadHandler* download_handler)
    : build_dir_(GetBuildDir()), download_handler_(download_handler) {}

SystemSymbols::~SystemSymbols() {
  // Disown any remaining ModuleRefs so they don't call us back.
  for (auto& pair : modules_)
    pair.second->SystemSymbolsDeleting();
  modules_.clear();
}

fxl::RefPtr<SystemSymbols::ModuleRef> SystemSymbols::InjectModuleForTesting(
    const std::string& build_id, std::unique_ptr<ModuleSymbols> module) {
  // Can't inject a module that already exists.
  FXL_DCHECK(modules_.find(build_id) == modules_.end());

  fxl::RefPtr<ModuleRef> result =
      fxl::MakeRefCounted<ModuleRef>(this, std::move(module));
  modules_[build_id] = result.get();
  return result;
}

Err SystemSymbols::GetModule(const std::string& build_id,
                             fxl::RefPtr<ModuleRef>* module) {
  auto found_existing = modules_.find(build_id);
  if (found_existing != modules_.end()) {
    *module = fxl::RefPtr<ModuleRef>(found_existing->second);
    return Err();
  }

  std::string file_name = build_id_index_.FileForBuildID(build_id);
  if (file_name.empty()) {
    // This should only be null in tests.
    FXL_DCHECK(download_handler_);

    *module = nullptr;
    download_handler_->RequestDownload(build_id, false);
    return Err();
  }

  auto module_symbols =
      std::make_unique<ModuleSymbolsImpl>(file_name, build_id);
  Err err = module_symbols->Load();
  if (err.has_error())
    return err;

  *module = fxl::MakeRefCounted<ModuleRef>(this, std::move(module_symbols));
  modules_[build_id] = module->get();
  return Err();
}

void SystemSymbols::WillDeleteModule(ModuleRef* module) {
  // We expect relatively few total modules and removing them is also uncommon,
  // so this is a brute-force search.
  for (auto iter = modules_.begin(); iter != modules_.end(); ++iter) {
    if (iter->second == module) {
      modules_.erase(iter);
      return;
    }
  }
  FXL_NOTREACHED();  // Notified for unknown ModuleRef.
}

}  // namespace zxdb
