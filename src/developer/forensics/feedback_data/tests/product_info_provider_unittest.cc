// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/annotations/product_info_provider.h"

#include <fuchsia/hwinfo/cpp/fidl.h>
#include <fuchsia/intl/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <map>
#include <memory>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/stubs/product_info_provider.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/cobalt/event.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/timekeeper/test_clock.h"

namespace forensics {
namespace feedback_data {
namespace {

using fuchsia::hwinfo::ProductInfo;
using fuchsia::intl::LocaleId;
using fuchsia::intl::RegulatoryDomain;
using fxl::SplitResult::kSplitWantNonEmpty;
using fxl::WhiteSpaceHandling::kTrimWhitespace;
using sys::testing::ServiceDirectoryProvider;
using testing::ElementsAreArray;
using testing::IsEmpty;
using testing::Pair;

class ProductInfoProviderTest
    : public UnitTestFixture,
      public testing::WithParamInterface<std::map<AnnotationKey, std::string>> {
 public:
  ProductInfoProviderTest() : executor_(dispatcher()) {}

 protected:
  void SetUpProductProviderServer(std::unique_ptr<stubs::ProductInfoProviderBase> server) {
    product_provider_server_ = std::move(server);
    if (product_provider_server_) {
      InjectServiceProvider(product_provider_server_.get());
    }
  }

  Annotations GetProductInfo(const AnnotationKeys& allowlist = {},
                             const zx::duration timeout = zx::sec(1)) {
    SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());
    cobalt::Logger cobalt(dispatcher(), services(), &clock_);

    ProductInfoProvider provider(dispatcher(), services(), &cobalt);
    auto promise = provider.GetAnnotations(timeout, allowlist);

    Annotations annotations;
    executor_.schedule_task(
        std::move(promise).then([&annotations](::fpromise::result<Annotations>& res) {
          if (res.is_ok()) {
            annotations = res.take_value();
          }
        }));
    RunLoopFor(timeout);

    return annotations;
  }

  async::Executor executor_;

 private:
  timekeeper::TestClock clock_;
  std::unique_ptr<stubs::ProductInfoProviderBase> product_provider_server_;
};

ProductInfo CreateProductInfo(const std::map<AnnotationKey, std::string>& annotations) {
  ProductInfo info;

  for (const auto& [key, value] : annotations) {
    if (key == kAnnotationHardwareProductSKU) {
      info.set_sku(value);
    } else if (key == kAnnotationHardwareProductLanguage) {
      info.set_language(value);
    } else if (key == kAnnotationHardwareProductRegulatoryDomain) {
      RegulatoryDomain domain;
      domain.set_country_code(value);
      info.set_regulatory_domain(std::move(domain));
    } else if (key == kAnnotationHardwareProductLocaleList) {
      auto locale_strings = fxl::SplitStringCopy(value, ",", kTrimWhitespace, kSplitWantNonEmpty);
      std::vector<LocaleId> locales;
      for (const auto& locale : locale_strings) {
        locales.emplace_back();
        locales.back().id = locale;
      }

      info.set_locale_list(locales);
    } else if (key == kAnnotationHardwareProductName) {
      info.set_name(value);
    } else if (key == kAnnotationHardwareProductModel) {
      info.set_model(value);
    } else if (key == kAnnotationHardwareProductManufacturer) {
      info.set_manufacturer(value);
    }
  }

  return info;
}

TEST_F(ProductInfoProviderTest, Check_OnlyGetRequestedAnnotations) {
  SetUpProductProviderServer(std::make_unique<stubs::ProductInfoProvider>(CreateProductInfo({
      {kAnnotationHardwareProductLanguage, "some-language"},
      {kAnnotationHardwareProductLocaleList, "some-locale1, some-locale2, some-locale3"},
      {kAnnotationHardwareProductManufacturer, "some-manufacturer"},
      {kAnnotationHardwareProductModel, "some-model"},
      {kAnnotationHardwareProductName, "some-name"},
      {kAnnotationHardwareProductRegulatoryDomain, "some-country-code"},
      {kAnnotationHardwareProductSKU, "some-sku"},
  })));

  auto product_info = GetProductInfo(/*allowlist=*/{
      kAnnotationHardwareProductSKU,
      kAnnotationHardwareProductModel,
  });
  EXPECT_THAT(product_info, ElementsAreArray({
                                Pair(kAnnotationHardwareProductModel, "some-model"),
                                Pair(kAnnotationHardwareProductSKU, "some-sku"),
                            }));
}

TEST_F(ProductInfoProviderTest, Check_BadKeyNotInAnnotations) {
  SetUpProductProviderServer(std::make_unique<stubs::ProductInfoProvider>(CreateProductInfo({
      {kAnnotationHardwareProductLanguage, "some-language"},
      {kAnnotationHardwareProductLocaleList, "some-locale1, some-locale2, some-locale3"},
      {kAnnotationHardwareProductManufacturer, "some-manufacturer"},
      {kAnnotationHardwareProductModel, "some-model"},
      {kAnnotationHardwareProductName, "some-name"},
      {kAnnotationHardwareProductRegulatoryDomain, "some-country-code"},
      {kAnnotationHardwareProductSKU, "some-sku"},
  })));

  auto product_info = GetProductInfo(/*allowlist=*/{
      kAnnotationHardwareProductSKU,
      kAnnotationHardwareProductModel,
      "bad_annotation",
  });

  EXPECT_THAT(product_info, ElementsAreArray({
                                Pair(kAnnotationHardwareProductModel, "some-model"),
                                Pair(kAnnotationHardwareProductSKU, "some-sku"),
                            }));
}

TEST_F(ProductInfoProviderTest, Succeed_ProductInfoReturnsFewerAnnotations) {
  SetUpProductProviderServer(std::make_unique<stubs::ProductInfoProvider>(CreateProductInfo({
      {kAnnotationHardwareProductSKU, "some-sku"},
  })));

  auto product_info = GetProductInfo(/*allowlist=*/{
      kAnnotationHardwareProductSKU,
      kAnnotationHardwareProductLanguage,
  });

  EXPECT_THAT(product_info,
              ElementsAreArray({
                  Pair(kAnnotationHardwareProductLanguage, AnnotationOr(Error::kMissingValue)),
                  Pair(kAnnotationHardwareProductSKU, AnnotationOr("some-sku")),
              }));
}

TEST_F(ProductInfoProviderTest, Succeed_NoRequestedKeysInAllowlist) {
  SetUpProductProviderServer(std::make_unique<stubs::ProductInfoProvider>(CreateProductInfo({
      {kAnnotationHardwareProductLanguage, "some-language"},
      {kAnnotationHardwareProductLocaleList, "some-locale1, some-locale2, some-locale3"},
      {kAnnotationHardwareProductManufacturer, "some-manufacturer"},
      {kAnnotationHardwareProductModel, "some-model"},
      {kAnnotationHardwareProductName, "some-name"},
      {kAnnotationHardwareProductRegulatoryDomain, "some-country-code"},
      {kAnnotationHardwareProductSKU, "some-sku"},
  })));

  auto product_info = GetProductInfo(/*allowlist=*/{
      "not-returned-by-product-provider",
  });

  EXPECT_THAT(product_info, IsEmpty());
}

TEST_F(ProductInfoProviderTest, Check_CobaltLogsTimeout) {
  SetUpProductProviderServer(std::make_unique<stubs::ProductInfoProviderNeverReturns>());

  auto product_info = GetProductInfo(/*allowlist=*/{
      kAnnotationHardwareProductSKU,
  });

  EXPECT_THAT(product_info, ElementsAreArray({
                                Pair(kAnnotationHardwareProductSKU, Error::kTimeout),
                            }));
  EXPECT_THAT(ReceivedCobaltEvents(), ElementsAreArray({
                                          cobalt::Event(cobalt::TimedOutData::kProductInfo),
                                      }));
}

const std::map<AnnotationKey, std::string> ProductInfoValues = {
    {kAnnotationHardwareProductSKU, "some-sku"},
    {kAnnotationHardwareProductLanguage, "some-language"},
    {kAnnotationHardwareProductRegulatoryDomain, "some-country-code"},
    {kAnnotationHardwareProductLocaleList, "some-locale1, some-locale2, some-locale3"},
    {kAnnotationHardwareProductName, "some-name"},
    {kAnnotationHardwareProductModel, "some-model"},
    {kAnnotationHardwareProductManufacturer, "some-manufacturer"},
};

std::vector<std::map<AnnotationKey, std::string>> GetProductInfoMapsWithOneKey() {
  std::vector<std::map<AnnotationKey, std::string>> maps;

  for (const auto& [key, value] : ProductInfoValues) {
    maps.push_back({{key, value}});
  }

  return maps;
}

std::vector<std::map<AnnotationKey, std::string>> GetProductInfosToTest() {
  auto maps = GetProductInfoMapsWithOneKey();
  maps.push_back(ProductInfoValues);
  return maps;
}

// Return all of the strings after the last '.' in each key concatenated together in camelCase.
std::string GetTestCaseName(
    const testing::TestParamInfo<std::map<AnnotationKey, std::string>>& info) {
  bool is_first = true;
  std::string name;
  for (const auto& [key, _] : info.param) {
    std::string key_suffix = key.substr(key.find_last_of(".") + 1);

    // Remove any '-'.
    key_suffix.erase(std::remove(key_suffix.begin(), key_suffix.end(), '-'), key_suffix.end());

    // If this isn't the first key in the map, convert the first letter of the string to uppercase.
    if (!is_first) {
      key_suffix[0] -= ('a' - 'A');
    }
    name += key_suffix;
    is_first = false;
  }
  return name;
}

INSTANTIATE_TEST_SUITE_P(WithVariousProductInfoResponses, ProductInfoProviderTest,
                         testing::ValuesIn(GetProductInfosToTest()), &GetTestCaseName);

TEST_P(ProductInfoProviderTest, Succeed_OnAnnotations) {
  std::map<AnnotationKey, std::string> annotations = GetParam();
  SetUpProductProviderServer(
      std::make_unique<stubs::ProductInfoProvider>(CreateProductInfo(annotations)));

  AnnotationKeys keys;
  for (const auto& [key, _] : annotations) {
    keys.insert(key);
  }

  auto product_info = GetProductInfo(/*allowlist=*/keys);
  EXPECT_EQ(product_info.size(), annotations.size());
  for (const auto& [key, value] : annotations) {
    EXPECT_EQ(product_info.at(key), value);
  }
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
