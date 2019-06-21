// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by app BSD-style license that can be
// found in the LICENSE file.

// See README.md for details and usage examples.

#include <string.h>
#include <zircon/errors.h>

#include <filesystem>
#include <iostream>

#include "src/lib/files/directory.h"
#include "src/lib/icu_data/cpp/icu_data.h"
#include "third_party/icu/source/common/unicode/utypes.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"
#include "zircon/assert.h"

using icu::TimeZone;

// Default directory for timezone .res files that can be loaded by icu_data
// library.
constexpr char kTzdataDir[] = "/config/data/tzdata/icu/44/le";

constexpr char kUseTzdataArg[] = "--use-tzdata";

int main(int argc, const char** argv) {
  bool use_tzdata = (argc > 1 && strcmp(argv[1], kUseTzdataArg) == 0);

  if (use_tzdata) {
    if (!files::IsDirectory(kTzdataDir)) {
      std::cerr
          << "Error: tzdata directory \"" << kTzdataDir << "\" doesn't exist.\n"
          << "Does the product you're building have a config_data rule to "
             "supply it?"
          << std::endl;
      return ZX_ERR_NOT_FOUND;
    }
    ZX_ASSERT(icu_data::InitializeWithTzResourceDir(kTzdataDir) == ZX_OK);
  } else {
    ZX_ASSERT(icu_data::Initialize() == ZX_OK);
  }

  UErrorCode err = U_ZERO_ERROR;
  const char* version = TimeZone::getTZDataVersion(err);

  if (err != U_ZERO_ERROR) {
    std::cerr << "Error: " << u_errorName(err) << std::endl;
    return ZX_ERR_INTERNAL;
  }

  std::string source = use_tzdata ? "tz .res files" : "icudtl.dat";
  std::cout << "Squawk! TZ version (from  " << source << ") is: \n"
            << version << std::endl;

  return ZX_OK;
}
