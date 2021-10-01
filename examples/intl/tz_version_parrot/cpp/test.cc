// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/errors.h>
#include <zircon/types.h>

#include <iostream>

#include <gtest/gtest.h>

#include "src/lib/files/directory.h"
// [START imports]
#include "src/lib/icu_data/cpp/icu_data.h"
// [END imports]
#include "third_party/icu/source/common/unicode/utypes.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"

using icu::TimeZone;

namespace {

TEST(TzVersionParrotTest, NoTzResFiles) {
  // [START loader_example]
  const auto result = icu_data::Initialize();
  ASSERT_EQ(result, ZX_OK) << "icu_data::Initialize failed";
  // [END loader_example]

  UErrorCode err = U_ZERO_ERROR;
  const char* version = TimeZone::getTZDataVersion(err);
  ASSERT_EQ(err, U_ZERO_ERROR) << "Error: " << u_errorName(err);

  std::cout << "Squawk! TZ version is: " << version << std::endl;
}

TEST(TzVersionParrotTest, WithTzResFiles) {
  // [START loader_config_example]
  // Default directory for timezone .res files
  constexpr char kDefaultTzdataDir[] = "/config/data/tzdata/icu/44/le";
  // Path to file containing the expected time zone database revision ID.
  constexpr char kDefaultTzRevisionFilePath[] = "/config/data/tzdata/revision.txt";
  ASSERT_TRUE(files::IsDirectory(kDefaultTzdataDir))
      << "Error: tzdata directory \"" << kDefaultTzdataDir << "\" doesn't exist.";

  const auto result = icu_data::InitializeWithTzResourceDirAndValidate(kDefaultTzdataDir,
                                                                       kDefaultTzRevisionFilePath);
  ASSERT_EQ(result, ZX_OK) << "icu_data::InitializeWithTzResourceDirAndValidate failed";
  // [END loader_config_example]

  UErrorCode err = U_ZERO_ERROR;
  const char* version = TimeZone::getTZDataVersion(err);
  ASSERT_EQ(err, U_ZERO_ERROR) << "Error: " << u_errorName(err);

  std::cout << "Squawk! TZ version is: " << version << std::endl;
}

TEST(TzVersionParrotTest, WithTzResFilesWrongRevision) {
  // Default directory for timezone .res files
  constexpr char kDefaultTzdataDir[] = "/config/data/tzdata/icu/44/le";
  // Path to file containing the expected time zone database revision ID.
  constexpr char kLocalTzRevisionFilePath[] = "/pkg/data/newer_revision.txt";
  ASSERT_TRUE(files::IsDirectory(kDefaultTzdataDir))
      << "Error: tzdata directory \"" << kDefaultTzdataDir << "\" doesn't exist.";

  const auto result =
      icu_data::InitializeWithTzResourceDirAndValidate(kDefaultTzdataDir, kLocalTzRevisionFilePath);
  ASSERT_EQ(result, ZX_ERR_IO_DATA_INTEGRITY)
      << "icu_data::InitializeWithTzResourceDirAndValidate failed";

  UErrorCode err = U_ZERO_ERROR;
  const char* version = TimeZone::getTZDataVersion(err);
  ASSERT_EQ(err, U_ZERO_ERROR) << "Error: " << u_errorName(err);

  std::cout << "Squawk! TZ version is: " << version << std::endl;
}

}  // namespace
