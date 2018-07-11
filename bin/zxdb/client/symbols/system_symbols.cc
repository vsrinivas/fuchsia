// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/system_symbols.h"

#include "garnet/bin/zxdb/client/file_util.h"
#include "garnet/bin/zxdb/client/host_util.h"
#include "garnet/bin/zxdb/client/symbols/module_symbols_impl.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"
#include "garnet/public/lib/fxl/strings/string_view.h"
#include "garnet/public/lib/fxl/strings/trim.h"

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

SystemSymbols::SystemSymbols() : build_dir_(GetBuildDir()) {}

SystemSymbols::~SystemSymbols() {
  // Disown any remaining ModuleRefs so they don't call us back.
  for (auto& pair : modules_)
    pair.second->SystemSymbolsDeleting();
  modules_.clear();
}

bool SystemSymbols::LoadBuildIDFile(std::string* msg) {
  std::string file_name = CatPathComponents(build_dir_, "ids.txt");
  FILE* id_file = fopen(file_name.c_str(), "r");
  if (!id_file) {
    *msg = "Build ID file not found: " + file_name;
    return false;
  }

  fseek(id_file, 0, SEEK_END);
  long length = ftell(id_file);
  if (length <= 0) {
    *msg = "Could not load build ID file: " + file_name;
    return false;
  }

  std::string contents;
  contents.resize(length);

  fseek(id_file, 0, SEEK_SET);
  if (fread(&contents[0], 1, contents.size(), id_file) !=
      static_cast<size_t>(length)) {
    *msg = "Could not load build ID file: " + file_name;
    return false;
  }

  fclose(id_file);
  build_id_to_file_ = ParseIds(contents);

  *msg = fxl::StringPrintf("Loaded %zu system symbol mappings from:\n  %s",
                           build_id_to_file_.size(), file_name.c_str());
  return true;
}

void SystemSymbols::AddBuildIDToFileMapping(const std::string& build_id,
                                            const std::string& file) {
  build_id_to_file_[build_id] = file;
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

Err SystemSymbols::GetModule(const std::string& name_for_msg,
                             const std::string& build_id,
                             fxl::RefPtr<ModuleRef>* module) {
  auto found_existing = modules_.find(build_id);
  if (found_existing != modules_.end()) {
    *module = fxl::RefPtr<ModuleRef>(found_existing->second);
    return Err();
  }

  auto found_id = build_id_to_file_.find(build_id);
  if (found_id == build_id_to_file_.end()) {
    return Err(fxl::StringPrintf(
        "Could not load symbols for \"%s\" because there was no mapping for "
        "build ID \"%s\".",
        name_for_msg.c_str(), build_id.c_str()));
  }

  auto module_symbols = std::make_unique<ModuleSymbolsImpl>(found_id->second);
  Err err = module_symbols->Load();
  if (err.has_error())
    return err;

  *module = fxl::MakeRefCounted<ModuleRef>(this, std::move(module_symbols));
  modules_[build_id] = module->get();
  return Err();
}

// static
std::map<std::string, std::string> SystemSymbols::ParseIds(
    const std::string& input) {
  std::map<std::string, std::string> result;

  for (size_t line_begin = 0; line_begin < input.size(); line_begin++) {
    size_t newline = input.find('\n', line_begin);
    if (newline == std::string::npos)
      newline = input.size();

    fxl::StringView line(&input[line_begin], newline - line_begin);
    if (!line.empty()) {
      // Format is <buildid> <space> <filename>
      size_t first_space = line.find(' ');
      if (first_space != std::string::npos && first_space > 0 &&
          first_space + 1 < line.size()) {
        // There is a space and it separates two nonempty things.
        fxl::StringView to_trim(" \t\r\n");
        fxl::StringView build_id =
            fxl::TrimString(line.substr(0, first_space), to_trim);
        fxl::StringView path = fxl::TrimString(
            line.substr(first_space + 1, line.size() - first_space - 1),
            to_trim);

        result.emplace(std::piecewise_construct,
                       std::forward_as_tuple(build_id.data(), build_id.size()),
                       std::forward_as_tuple(path.data(), path.size()));
      }
    }

    line_begin = newline;  // The for loop will advance past this.
  }
  return result;
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
