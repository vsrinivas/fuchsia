// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/errors.h>
#include <zircon/types.h>

#include <iostream>
#include <string>

#include <gtest/gtest.h>

#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/icu_data/cpp/icu_data.h"
#include "src/tests/intl/tzdata/icu_headers.h"

namespace {

std::string GetTzdataVersion() {
  constexpr char kTzdataDir[] = "/tzdata-icu-44-le";
  constexpr char kTzdataRevisionFilePath[] = "/tzdata-icu-44-le/revision.txt";

  const auto result =
      icu_data::InitializeWithTzResourceDirAndValidate(kTzdataDir, kTzdataRevisionFilePath);
  if (result != ZX_OK) {
    std::cerr << "icu_data::InitializeWithTzResourceDirAndValidate failed";
    return "";
  }

  UErrorCode err = U_ZERO_ERROR;
  const char* tzdata_version = icu::TimeZone::getTZDataVersion(err);
  if (err != U_ZERO_ERROR) {
    std::cerr << "Error: " << u_errorName(err);
    return "";
  }

  return std::string(tzdata_version);
}

// This doesn't actually rely on the Abseil library (which has an outdated version in the Fuchsia
// tree and is slated for removal).
std::string GetZoneinfoVersion() {
  constexpr char kZoneinfoRevisionPath[] = "/config/data/tzdata/revision.txt";
  std::string zoneinfo_version;
  if (!files::ReadFileToString(kZoneinfoRevisionPath, &zoneinfo_version)) {
    std::cerr << "Failed to read version from " << kZoneinfoRevisionPath;
  }
  return zoneinfo_version;
}

TEST(TzDataZoneInfoTest, VersionsMatch) {
  std::string tzdata_version = GetTzdataVersion();
  std::string zoneinfo_version = GetZoneinfoVersion();

  // Sometimes the upstream data providers add suffixes to the version IDs reflecting minor fixups,
  // which is fine. However, the year and letter (e.g. "2021a") must match.
  ASSERT_GE(tzdata_version.length(), 5U) << "tzdata version: " << tzdata_version;
  ASSERT_GE(zoneinfo_version.length(), 5U) << "zoneinfo version: " << zoneinfo_version;
  ASSERT_EQ(tzdata_version.substr(0, 5), zoneinfo_version.substr(0, 5))
      << "tzdata: " << tzdata_version << " | zoneinfo: " << zoneinfo_version;
}
}  // namespace
