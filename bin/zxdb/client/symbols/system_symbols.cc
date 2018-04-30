// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/system_symbols.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "garnet/public/lib/fxl/strings/string_printf.h"
#include "garnet/public/lib/fxl/strings/string_view.h"
#include "garnet/public/lib/fxl/strings/trim.h"
#include "llvm/DebugInfo/Symbolize/Symbolize.h"

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace zxdb {

namespace {

// Returns the path to the current binary.
std::string GetSelfPath() {
  std::string result;
#if defined(__APPLE__)
  // Executable path can have relative references ("..") depending on how the
  // app was launched.
  uint32_t length = 0;
  _NSGetExecutablePath(nullptr, &length);
  result.resize(length);
  _NSGetExecutablePath(&result[0], &length);
  result.resize(length - 1);  // Length included terminator.
#elif defined(__linux__)
  // The realpath() call below will resolve the symbolic link.
  result.assign("/proc/self/exe");
#else
#error Write this for your platform.
#endif

  char fullpath[PATH_MAX];
  return std::string(realpath(result.c_str(), fullpath));
}

// Returns the file path of the "ids.txt" file that maps build IDs to file
// paths for the current build.
//
// TODO(brettw) this is hardcoded and will only work in a full local build.
// We will need a more flexible way to do handle this, and also a way to
// explicitly specify a location for the mapping file.
std::string GetIdFilePath() {
  // Expect the debugger to be in "<build>/host_x64/zxdb" and the build ID
  // mapping file to be in "<build>/ids.txt".
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

  path.append("ids.txt");
  return path;
}

}  // namespace

SystemSymbols::SystemSymbols() = default;
SystemSymbols::~SystemSymbols() = default;

bool SystemSymbols::Init(std::string* status) {
  llvm::symbolize::LLVMSymbolizer::Options opts(
      llvm::symbolize::FunctionNameKind::LinkageName,
      true,          // symbol table
      true,          // demangle
      false,         // relative
      std::string()  // default arch
      );
  symbolizer_ = std::make_unique<llvm::symbolize::LLVMSymbolizer>(opts);

  // Load build ID mapping.
  std::string id_file_path = GetIdFilePath();
  if (!LoadBuildIDFile(id_file_path) || build_id_to_file_.empty()) {
    *status = "Warning: unable to load build ID mapping file: " + id_file_path;
    return false;
  }

  *status = fxl::StringPrintf("Loaded %d system symbol mappings from \"%s\".",
                           static_cast<int>(build_id_to_file_.size()),
                           id_file_path.c_str());
  return true;
}

bool SystemSymbols::LoadBuildIDFile(const std::string& file_name) {
  FILE* id_file = fopen(file_name.c_str(), "r");
  if (!id_file)
    return false;

  fseek(id_file, 0, SEEK_END);
  long length = ftell(id_file);
  if (length <= 0)
    return false;

  std::string contents;
  contents.resize(length);

  fseek(id_file, 0, SEEK_SET);
  if (fread(&contents[0], 1, contents.size(), id_file) !=
      static_cast<size_t>(length))
    return false;

  fclose(id_file);
  build_id_to_file_ = ParseIds(contents);
  return true;
}

std::string SystemSymbols::BuildIDToPath(const std::string& build_id) const {
  auto found = build_id_to_file_.find(build_id);
  if (found == build_id_to_file_.end())
    return std::string();
  return found->second;
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

}  // namespace zxdb
