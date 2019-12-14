// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by app BSD-style license that can be
// found in the LICENSE file.

#include "tz_version.h"

#include <iostream>

#include "src/lib/files/file.h"
#include "third_party/icu/source/common/unicode/utypes.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"

constexpr ssize_t kTzVersionLength = 5;

int icu_data_extractor::ExtractTzVersion(std::optional<std::string> output_path) {
  UErrorCode err = U_ZERO_ERROR;
  const char* version = icu::TimeZone::getTZDataVersion(err);

  if (err != U_ZERO_ERROR) {
    std::cerr << "Error: " << u_errorName(err) << std::endl;
    return -1;
  }

  if (strlen(version) != kTzVersionLength) {
    std::cerr << "Bad tz version string: " << version << std::endl;
    return -1;
  }

  if (output_path.has_value()) {
    if (!files::WriteFile(output_path.value(), version, kTzVersionLength)) {
      std::cerr << "Couldn't write to " << output_path.value() << std::endl;
      return -1;
    }
  } else {
    std::cout << version;
  }

  return 0;
}
