// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by app BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

#include "common.h"
#include "src/lib/fxl/command_line.h"
#include "third_party/icu/source/common/unicode/putil.h"
#include "third_party/icu/source/common/unicode/udata.h"
#include "third_party/icu/source/common/unicode/utypes.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"
#include "tz_ids.h"
#include "tz_version.h"

using icu_data_extractor::Command;
using icu_data_extractor::kArgIcuDataPath;
using icu_data_extractor::kArgTzResPath;
using icu_data_extractor::TzIds;
using icu_data_extractor::TzVersion;

// Maps a file into memory as read-only and returns a pointer to the memory.
//
// Returns `nullptr` if reading or mmapping fails.
void* MmapFile(const std::string& path) {
  int fd = open(path.c_str(), O_RDONLY);
  if (fd == -1) {
    return nullptr;
  }

  struct stat st;
  if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
    return nullptr;
  }

  return mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
}

int PrintUsage(const fxl::CommandLine& command_line,
               const std::vector<std::unique_ptr<Command>>& commands) {
  std::cout << "Usage: " << command_line.argv0() << " [OPTION]... COMMAND [COMMAND-OPTION]...\n\n"
            << "OPTIONS:\n"
            << "  --" << kArgIcuDataPath << "=FILE\t(required)\tPath to icudtl.dat\n"
            << "  --" << kArgTzResPath << "=DIR\t(required)\tPath to tzres directory\n"
            << "\n"
            << "COMMANDS:\n\n";

  for (const auto& command : commands) {
    command->PrintDocs(std::cout);
    std::cout << "\n\n\n";
  }

  std::cout << std::endl;

  return -1;
}

int main(int argc, const char** argv) {
  std::vector<std::unique_ptr<Command>> commands;
  commands.push_back(std::make_unique<TzVersion>());
  commands.push_back(std::make_unique<TzIds>());

  const std::vector<std::string> args(argv, argv + argc);
  std::vector<std::string>::const_iterator sub_first;
  const auto command_line =
      fxl::CommandLineFromIteratorsFindFirstPositionalArg(args.begin(), args.end(), &sub_first);

  std::string icu_data_path;
  if (!command_line.GetOptionValue(kArgIcuDataPath, &icu_data_path)) {
    return PrintUsage(command_line, commands);
  }

  std::optional<std::string> tz_res_path = std::nullopt;
  if (command_line.HasOption(kArgTzResPath)) {
    std::string tz_res_path_str;
    command_line.GetOptionValue(kArgTzResPath, &tz_res_path_str);
    tz_res_path = tz_res_path_str;
    setenv("ICU_TIMEZONE_FILES_DIR", tz_res_path_str.c_str(), 1);
  }

  // This will be unmapped automatically when the program exits.
  const void* icu_data = MmapFile(icu_data_path);
  if (icu_data == nullptr) {
    std::cerr << "Couldn't read file at " << icu_data_path << std::endl;
    return -1;
  }

  UErrorCode err = U_ZERO_ERROR;
  udata_setCommonData(icu_data, &err);
  if (err != U_ZERO_ERROR) {
    std::cerr << "Error while loading from \"" << icu_data_path << "\": " << u_errorName(err)
              << std::endl;
    return -1;
  }

  if (command_line.positional_args().size() < 1) {
    return PrintUsage(command_line, commands);
  }

  const auto sub_command_line = fxl::CommandLineFromIterators(sub_first, args.end());
  const auto command_name = sub_command_line.argv0();

  // For two items, linear search is fast enough.
  const auto command_result = std::find_if(
      commands.begin(), commands.end(), [&command_name](const std::unique_ptr<Command>& command) {
        return command->Name() == command_name;
      });

  if (command_result != commands.end()) {
    return (*command_result)->Execute(command_line, sub_command_line);
  } else {
    std::cerr << "Unknown command " << command_name << std::endl;
    return PrintUsage(command_line, commands);
  }
}
