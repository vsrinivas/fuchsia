// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by app BSD-style license that can be
// found in the LICENSE file.

#include "tz_version.h"

#include <iostream>
#include <sstream>

#include "common.h"
#include "src/lib/fxl/command_line.h"
#include "third_party/icu/source/common/unicode/utypes.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"

namespace icu_data_extractor {

namespace {

constexpr const char kName[] = "tz-version";

constexpr ssize_t kTzVersionLength = 5;

}  // namespace

std::string_view TzVersion::Name() const { return kName; }

int TzVersion::Execute(const fxl::CommandLine& command_line,
                       const fxl::CommandLine& sub_command_line) const {
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

  return WriteToOutputFileOrStdOut(sub_command_line, std::string(version));
}

void TzVersion::PrintDocs(std::ostream& os) const {
  os << "  " << kName << "\n    --" << kArgOutputPath
     << "=FILE\t\t\tPath to output file (if omitted, STDOUT)"
     << "\n\n  Extract the time zone version string, e.g. \"2019c\"";
}

}  // namespace icu_data_extractor
