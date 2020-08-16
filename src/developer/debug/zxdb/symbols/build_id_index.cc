// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/build_id_index.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <system_error>
#include <tuple>
#include <utility>

#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/lib/elflib/elflib.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/fxl/strings/trim.h"

namespace zxdb {

BuildIDIndex::Entry BuildIDIndex::EntryForBuildID(const std::string& build_id) {
  EnsureCacheClean();

  if (build_id_to_files_.find(build_id) == build_id_to_files_.end())
    SearchBuildIdDirs(build_id);

  // No matter whether SearchBuildIdDirs found the symbol or not, build_id_to_files_[build_id] will
  // always create the entry so next time no search will be performed.
  return build_id_to_files_[build_id];
}

void BuildIDIndex::SearchBuildIdDirs(const std::string& build_id) {
  if (build_id.size() <= 2) {
    return;
  }

  auto path = build_id.substr(0, 2) + "/" + build_id.substr(2);

  for (const auto& [source, build_dir] : build_id_dirs_) {
    // There are potentially two files, one with just the build ID, one with a ".debug" suffix. The
    // ".debug" suffix one is supposed to contain either just the DWARF symbols, or the full
    // unstripped binary. The plain one is supposed to be either a stripped or unstripped binary.
    //
    // Since we're looking for DWARF information, look in the ".debug" one first.
    IndexSourceFile(source + "/" + path + ".debug", build_dir);
    IndexSourceFile(source + "/" + path, build_dir);
  }
}

void BuildIDIndex::AddBuildIDMappingForTest(const std::string& build_id,
                                            const std::string& file_name) {
  // This map saves the manual mapping across cache updates.
  manual_mappings_[build_id].debug_info = file_name;
  manual_mappings_[build_id].binary = file_name;
  // Don't bother marking the cache dirty since we can just add it.
  build_id_to_files_[build_id].debug_info = file_name;
  build_id_to_files_[build_id].binary = file_name;
}

void BuildIDIndex::ClearAll() {
  ids_txts_.clear();
  build_id_dirs_.clear();
  symbol_index_files_.clear();
  sources_.clear();
  ClearCache();
}

bool BuildIDIndex::AddOneFile(const std::string& file_name) {
  return IndexSourceFile(file_name, "", true);
}

void BuildIDIndex::AddIdsTxt(const std::string& ids_txt, const std::string& build_dir) {
  // If the file is already loaded, ignore it.
  if (std::find_if(ids_txts_.begin(), ids_txts_.end(),
                   [&ids_txt](auto it) { return it.first == ids_txt; }) != ids_txts_.end())
    return;

  ids_txts_.emplace_back(ids_txt, build_dir);
  ClearCache();
}

void BuildIDIndex::AddBuildIdDir(const std::string& dir, const std::string& build_dir) {
  if (std::find_if(build_id_dirs_.begin(), build_id_dirs_.end(),
                   [&dir](auto it) { return it.first == dir; }) != build_id_dirs_.end())
    return;

  build_id_dirs_.emplace_back(dir, build_dir);
  ClearCache();
}

void BuildIDIndex::AddSymbolIndexFile(const std::string& path) {
  if (std::find(symbol_index_files_.begin(), symbol_index_files_.end(), path) !=
      symbol_index_files_.end())
    return;

  symbol_index_files_.push_back(path);
  ClearCache();
}

void BuildIDIndex::AddPlainFileOrDir(const std::string& path) {
  if (std::find(sources_.begin(), sources_.end(), path) != sources_.end())
    return;

  sources_.push_back(path);
  ClearCache();
}

BuildIDIndex::StatusList BuildIDIndex::GetStatus() {
  EnsureCacheClean();
  return status_;
}

void BuildIDIndex::ClearCache() { cache_dirty_ = true; }

// static
int BuildIDIndex::ParseIDs(const std::string& input, const std::filesystem::path& containing_dir,
                           const std::string& build_dir, BuildIDMap* output) {
  int added = 0;
  for (size_t line_begin = 0; line_begin < input.size(); line_begin++) {
    size_t newline = input.find('\n', line_begin);
    if (newline == std::string::npos)
      newline = input.size();

    std::string_view line(&input[line_begin], newline - line_begin);
    if (!line.empty()) {
      // Format is <buildid> <space> <filename>.
      size_t first_space = line.find(' ');
      if (first_space != std::string::npos && first_space > 0 && first_space + 1 < line.size()) {
        // There is a space and it separates two nonempty things.
        std::string_view to_trim(" \t\r\n");
        std::string_view build_id = fxl::TrimString(line.substr(0, first_space), to_trim);
        std::string_view path_data =
            fxl::TrimString(line.substr(first_space + 1, line.size() - first_space - 1), to_trim);

        std::filesystem::path path(path_data);

        if (path.is_relative()) {
          path = containing_dir / path;
        }

        BuildIDIndex::Entry entry;
        // Assume the file contains both debug info and program bits.
        entry.debug_info = path;
        entry.binary = path;
        entry.build_dir = build_dir;

        added++;
        output->emplace(std::piecewise_construct,
                        std::forward_as_tuple(build_id.data(), build_id.size()),
                        std::forward_as_tuple(entry));
      }
    }

    line_begin = newline;  // The for loop will advance past this.
  }
  return added;
}

void BuildIDIndex::LogMessage(const std::string& msg) const {
  if (information_callback_)
    information_callback_(msg);
}

void BuildIDIndex::LoadIdsTxt(const std::string& file_name, const std::string& build_dir) {
  std::error_code err;

  auto path = std::filesystem::canonical(file_name, err);

  if (err) {
    status_.emplace_back(file_name, 0);
    LogMessage("Can't open build ID file: " + file_name);
    return;
  }

  auto containing_dir = path.parent_path();

  FILE* id_file = fopen(file_name.c_str(), "r");
  if (!id_file) {
    status_.emplace_back(file_name, 0);
    LogMessage("Can't open build ID file: " + file_name);
    return;
  }

  fseek(id_file, 0, SEEK_END);
  long length = ftell(id_file);
  if (length <= 0) {
    status_.emplace_back(file_name, 0);
    LogMessage("Can't load build ID file: " + file_name);
    return;
  }

  std::string contents;
  contents.resize(length);

  fseek(id_file, 0, SEEK_SET);
  if (fread(&contents[0], 1, contents.size(), id_file) != static_cast<size_t>(length)) {
    status_.emplace_back(file_name, 0);
    LogMessage("Can't read build ID file: " + file_name);
    return;
  }

  fclose(id_file);

  int added = ParseIDs(contents, containing_dir, build_dir, &build_id_to_files_);
  status_.emplace_back(file_name, added);
  if (!added)
    LogMessage("No mappings found in build ID file: " + file_name);
}

void BuildIDIndex::LoadSymbolIndexFile(const std::string& file_name) {
  std::ifstream file(file_name);
  if (file.fail()) {
    return LogMessage("Cannot read symbol-index file: " + file_name);
  }

  bool need_purge = false;

  while (!file.eof()) {
    std::string line;
    std::string symbol_path;
    std::string build_dir;

    std::getline(file, line);
    if (file.fail()) {
      // If the file ends with \n, we will get failbit, eofbit and line == "".
      if (file.eof())
        break;
      return LogMessage("Error reading " + file_name);
    }

    if (auto tab_index = line.find('\t'); tab_index != std::string::npos) {
      symbol_path = line.substr(0, tab_index);
      build_dir = line.substr(tab_index + 1);
    } else {
      symbol_path = line;
      build_dir.clear();
    }

    // Both paths must be absolute.
    if (symbol_path.empty() || symbol_path[0] != '/' ||
        (!build_dir.empty() && build_dir[0] != '/')) {
      LogMessage(fxl::StringPrintf("Invalid line in %s: %s", file_name.c_str(), line.c_str()));
      continue;
    }

    std::error_code ec;
    if (std::filesystem::is_directory(symbol_path, ec)) {
      AddBuildIdDir(symbol_path, build_dir);
    } else if (std::filesystem::exists(symbol_path, ec)) {
      AddIdsTxt(symbol_path, build_dir);
    } else if (build_dir.empty() || !std::filesystem::exists(build_dir, ec)) {
      // Purge is needed only when
      // 1) build_dir is not provided and symbol_path doesn't exist.
      // 2) build_dir is provided and it doesn't exist.
      need_purge = true;
    }
  }

  if (need_purge) {
    LogMessage(
        "Your symbol-index file contains non-existent paths. Please consider running "
        "\"fx symbol-index purge\" to remove them.");
  }
}

void BuildIDIndex::IndexSourcePath(const std::string& path) {
  std::error_code ec;
  if (std::filesystem::is_directory(path, ec)) {
    // Iterate through all files in this directory, but don't recurse.
    int indexed = 0;
    for (const auto& child : std::filesystem::directory_iterator(path, ec)) {
      if (IndexSourceFile(child.path()))
        indexed++;
    }

    status_.emplace_back(path, indexed);
  } else if (!ec && IndexSourceFile(path)) {
    status_.emplace_back(path, 1);
  } else {
    status_.emplace_back(path, 0);
    LogMessage(fxl::StringPrintf("Symbol file could not be loaded: %s", path.c_str()));
  }
}

bool BuildIDIndex::IndexSourceFile(const std::string& file_path, const std::string& build_dir,
                                   bool preserve) {
  auto elf = elflib::ElfLib::Create(file_path);
  if (!elf)
    return false;

  std::string build_id = elf->GetGNUBuildID();
  if (build_id.empty())
    return false;

  auto ret = false;
  if (elf->ProbeHasDebugInfo() && build_id_to_files_[build_id].debug_info.empty()) {
    build_id_to_files_[build_id].debug_info = file_path;
    ret = true;
  }
  if (elf->ProbeHasProgramBits() && build_id_to_files_[build_id].binary.empty()) {
    build_id_to_files_[build_id].binary = file_path;
    ret = true;
  }

  if (ret && !build_dir.empty()) {
    build_id_to_files_[build_id].build_dir = build_dir;
  }

  if (ret && preserve) {
    manual_mappings_[build_id] = build_id_to_files_[build_id];
  }

  return ret;
}

void BuildIDIndex::EnsureCacheClean() {
  if (!cache_dirty_)
    return;

  status_.clear();
  build_id_to_files_ = manual_mappings_;

  for (const auto& symbol_index_file : symbol_index_files_)
    LoadSymbolIndexFile(symbol_index_file);

  for (const auto& source : sources_)
    IndexSourcePath(source);

  for (const auto& [ids_txt, build_dir] : ids_txts_)
    LoadIdsTxt(ids_txt, build_dir);

  for (const auto& [path, build_dir] : build_id_dirs_)
    status_.emplace_back(path, BuildIDIndex::kStatusIsFolder);

  cache_dirty_ = false;
}

}  // namespace zxdb
