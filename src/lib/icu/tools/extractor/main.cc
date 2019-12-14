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

#include "src/lib/fxl/command_line.h"
#include "third_party/icu/source/common/unicode/putil.h"
#include "third_party/icu/source/common/unicode/udata.h"
#include "third_party/icu/source/common/unicode/utypes.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"
#include "tz_version.h"

using icu_data_extractor::ExtractTzVersion;

// Options
constexpr char kArgIcuDataPath[] = "icu-data-file";
constexpr char kArgTzResPath[] = "tz-res-dir";
constexpr char kArgOutputPath[] = "output";

// Commands
constexpr char kCommandTzVersion[] = "tz-version";

int PrintUsage(std::string argv0) {
  std::cout << "Usage: " << argv0 << " [OPTION]... COMMAND\n\n"
            << "OPTIONS:\n"
            << "  --" << kArgIcuDataPath << "=FILE\t(required)\tPath to icudtl.dat\n"
            << "  --" << kArgTzResPath << "=DIR\t(required)\tPath to tzres directory\n"
            << "  --" << kArgOutputPath << "=FILE\t\t\t\tPath to output file (if omitted, STDOUT)"
            << "\n\n"
            << "COMMANDS:\n"
            << "  " << kCommandTzVersion
            << "\n\tExtract the time zone version string, e.g. \"2019c\"\n"
            << std::endl;
  return -1;
}

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

int main(int argc, const char** argv) {
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  const auto argv0 = command_line.argv0();

  std::string icu_data_path;
  if (!command_line.GetOptionValue(kArgIcuDataPath, &icu_data_path)) {
    return PrintUsage(argv0);
  }

  std::optional<std::string> tz_res_path = std::nullopt;
  if (command_line.HasOption(kArgTzResPath)) {
    std::string tz_res_path_str;
    command_line.GetOptionValue(kArgTzResPath, &tz_res_path_str);
    tz_res_path = tz_res_path_str;
    setenv("ICU_TIMEZONE_FILES_DIR", tz_res_path_str.c_str(), 1);
  }

  std::optional<std::string> output_path = std::nullopt;
  if (command_line.HasOption(kArgOutputPath)) {
    std::string output_path_str;
    command_line.GetOptionValue(kArgOutputPath, &output_path_str);
    output_path = output_path_str;
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
    return PrintUsage(command_line.argv0());
  }

  const auto command = command_line.positional_args()[0];
  if (command == kCommandTzVersion) {
    return ExtractTzVersion(output_path);
  } else {
    std::cerr << "Unknown command " << command << std::endl;
    return PrintUsage(argv0);
  }
}
