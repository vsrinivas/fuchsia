// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by app BSD-style license that can be
// found in the LICENSE file.

// See README.md for details and usage examples.

#include <string.h>
#include <zircon/errors.h>

#include <filesystem>
#include <iostream>

#include "src/lib/files/directory.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/icu_data/cpp/icu_data.h"
#include "third_party/icu/source/common/unicode/utypes.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"
#include "zircon/assert.h"

using icu::TimeZone;

// Default directory for timezone .res files that can be loaded by icu_data
// library.
constexpr char kDefaultTzdataDir[] = "/config/data/tzdata/icu/44/le";

// Path to file containing the expected time zone database revision ID.
constexpr char kDefaultTzRevisionFilePath[] = "/config/data/tzdata/revision.txt";

constexpr char kUseTzdataArg[] = "use-tzdata";
constexpr char kTzdataDirArg[] = "tzdata-dir";
constexpr char kTzRevisionFilePathArg[] = "tz-revision-file";

int main(int argc, const char** argv) {
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  const bool use_tzdata = command_line.HasOption(kUseTzdataArg);

  if (use_tzdata) {
    const auto tzdata_dir =
        command_line.GetOptionValueWithDefault(kTzdataDirArg, kDefaultTzdataDir);
    const auto tz_revision_file_path =
        command_line.GetOptionValueWithDefault(kTzRevisionFilePathArg, kDefaultTzRevisionFilePath);

    std::cout << "tzdata_dir:\t\t" << tzdata_dir << std::endl
              << "tz_revision_file_path:\t" << tz_revision_file_path << std::endl;

    if (!files::IsDirectory(tzdata_dir)) {
      std::cerr << "Error: tzdata directory \"" << tzdata_dir << "\" doesn't exist.\n"
                << "Does the product you're building have a config_data rule to "
                   "supply it?"
                << std::endl;
      return ZX_ERR_NOT_FOUND;
    }
    const auto result = icu_data::InitializeWithTzResourceDirAndValidate(
        tzdata_dir.c_str(), tz_revision_file_path.c_str());
    if (result != ZX_OK) {
      std::cerr << "icu_data::InitializeWithTzResourceDirAndValidate failed: " << result
                << std::endl;
      return result;
    }
  } else {
    const auto result = icu_data::Initialize();
    if (result != ZX_OK) {
      std::cerr << "icu_data::Initialize failed: " << result << std::endl;
      return result;
    }
  }

  UErrorCode err = U_ZERO_ERROR;
  const char* version = TimeZone::getTZDataVersion(err);

  if (err != U_ZERO_ERROR) {
    std::cerr << "Error: " << u_errorName(err) << std::endl;
    return ZX_ERR_INTERNAL;
  }

  std::string source = use_tzdata ? "tz .res files" : "icudtl.dat";
  std::cout << "Squawk! TZ version (from  " << source << ") is: \n" << version << std::endl;

  return ZX_OK;
}
