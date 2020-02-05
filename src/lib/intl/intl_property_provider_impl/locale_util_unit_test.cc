// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/intl/cpp/fidl.h>

#include "gtest/gtest.h"
#include "lib/fostr/fidl/fuchsia/intl/formatting.h"
#include "locale_util.h"
#include "src/lib/fidl_fuchsia_intl_ext/cpp/fidl_ext.h"
#include "src/lib/icu_data/cpp/icu_data.h"
#include "third_party/icu/source/common/unicode/localebuilder.h"
#include "third_party/icu/source/common/unicode/locid.h"

namespace intl {
namespace testing {
namespace {

using GTest = ::testing::Test;
using StringMap = std::map<std::string, std::string>;
using fuchsia::intl::CalendarId;

// Tests for LocaleUtil.
class LocaleUtilUnitTest : public GTest {
 public:
  static void SetUpTestSuite() {
    GTest::SetUpTestSuite();
    ASSERT_EQ(ZX_OK, icu_data::Initialize());
  }
};

TEST_F(LocaleUtilUnitTest, LocaleIdToIcuLocale) {
  UErrorCode error_code = U_ZERO_ERROR;
  icu::LocaleBuilder expected_builder{};
  expected_builder.setLanguageTag("fr-CA-u-ca-islam-fw-wed-tz-uslax");

  const icu::Locale expected = expected_builder.build(error_code);
  ASSERT_TRUE(U_SUCCESS(error_code));

  ASSERT_EQ(expected, intl::LocaleIdToIcuLocale(
                          "fr-CA", StringMap{{"ca", "islam"}, {"fw", "wed"}, {"tz", "uslax"}})
                          .value());
}

TEST_F(LocaleUtilUnitTest, ExpandLocaleId_ExpandsCorrectly) {
  UErrorCode error_code = U_ZERO_ERROR;
  icu::Locale locale = icu::Locale::forLanguageTag("en-US-u-tz-usnyc", error_code);

  const std::string expected = "en-US-u-ca-gregory-fw-sun-hc-h12-ms-ussystem-nu-latn-tz-usnyc";
  const std::string actual = ExpandLocaleId(locale).value().id;
  ASSERT_EQ(expected, actual);
}

TEST_F(LocaleUtilUnitTest, ExpandLocaleId_PreservesExistingOverrides) {
  UErrorCode error_code = U_ZERO_ERROR;
  icu::Locale locale = icu::Locale::forLanguageTag(
      "en-CA-u-ca-hebrew-hc-h23-ms-metric-nu-deva-tz-usnyc", error_code);

  const std::string expected = "en-CA-u-ca-hebrew-fw-sun-hc-h23-ms-metric-nu-deva-tz-usnyc";
  const std::string actual = ExpandLocaleId(locale).value().id;
  ASSERT_EQ(expected, actual);
}

TEST_F(LocaleUtilUnitTest, ExtractBcp47CalendarId_Valid) {
  auto result = intl::ExtractBcp47CalendarId(CalendarId{.id = "und-u-ca-gregory"});
  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(std::string("gregory"), result.value());
}

TEST_F(LocaleUtilUnitTest, ExtractBcp47CalendarId_Invalid) {
  ASSERT_TRUE(intl::ExtractBcp47CalendarId(CalendarId{.id = "und-u-cagregory"}).is_error());
}

}  // namespace
}  // namespace testing
}  // namespace intl
