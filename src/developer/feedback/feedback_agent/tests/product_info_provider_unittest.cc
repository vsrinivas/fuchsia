// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/annotations/product_info_provider.h"

#include <fuchsia/hwinfo/cpp/fidl.h>
#include <fuchsia/intl/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <map>
#include <memory>
#include <string>

#include "src/developer/feedback/feedback_agent/annotations/aliases.h"
#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/developer/feedback/testing/cobalt_test_fixture.h"
#include "src/developer/feedback/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/feedback/testing/stubs/product_info_provider.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/developer/feedback/utils/cobalt.h"
#include "src/developer/feedback/utils/cobalt_event.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/split_string.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

using fuchsia::hwinfo::ProductInfo;
using fuchsia::intl::LocaleId;
using fuchsia::intl::RegulatoryDomain;
using fxl::SplitResult::kSplitWantNonEmpty;
using fxl::WhiteSpaceHandling::kTrimWhitespace;
using sys::testing::ServiceDirectoryProvider;
using testing::ElementsAreArray;
using testing::Pair;

class ProductInfoProviderTest : public UnitTestFixture,
                                public CobaltTestFixture,
                                public testing::WithParamInterface<Annotations> {
 public:
  ProductInfoProviderTest()
      : CobaltTestFixture(/*unit_test_fixture=*/this), executor_(dispatcher()) {}

 protected:
  void SetUpProductProvider(std::unique_ptr<stubs::ProductInfoProvider> product_provider) {
    product_provider_ = std::move(product_provider);
    if (product_provider_) {
      InjectServiceProvider(product_provider_.get());
    }
  }

  Annotations GetProductInfo(const AnnotationKeys& annotations_to_get = {},
                             const zx::duration timeout = zx::sec(1)) {
    SetUpCobaltLoggerFactory(std::make_unique<stubs::CobaltLoggerFactory>());
    Cobalt cobalt(dispatcher(), services());

    ProductInfoProvider provider(annotations_to_get, dispatcher(), services(), timeout, &cobalt);
    auto promise = provider.GetAnnotations();

    Annotations annotations;
    executor_.schedule_task(std::move(promise).then([&annotations](fit::result<Annotations>& res) {
      if (res.is_ok()) {
        annotations = res.take_value();
      }
    }));
    RunLoopFor(timeout);

    if (annotations.empty()) {
      return {};
    }

    Annotations product_info;
    for (auto& [key, value] : annotations) {
      product_info[key] = std::move(value);
    }

    return product_info;
  }

  async::Executor executor_;

 private:
  std::unique_ptr<stubs::ProductInfoProvider> product_provider_;
};

ProductInfo CreateProductInfo(const Annotations& annotations) {
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
  SetUpProductProvider(std::make_unique<stubs::ProductInfoProvider>(CreateProductInfo({
      {kAnnotationHardwareProductLanguage, "some-language"},
      {kAnnotationHardwareProductLocaleList, "some-locale1, some-locale2, some-locale3"},
      {kAnnotationHardwareProductManufacturer, "some-manufacturer"},
      {kAnnotationHardwareProductModel, "some-model"},
      {kAnnotationHardwareProductName, "some-name"},
      {kAnnotationHardwareProductRegulatoryDomain, "some-country-code"},
      {kAnnotationHardwareProductSKU, "some-sku"},
  })));

  auto product_info = GetProductInfo({
      kAnnotationHardwareProductSKU,
      kAnnotationHardwareProductModel,
  });
  EXPECT_THAT(product_info, ElementsAreArray({
                                Pair(kAnnotationHardwareProductModel, "some-model"),
                                Pair(kAnnotationHardwareProductSKU, "some-sku"),
                            }));
}

TEST_F(ProductInfoProviderTest, Check_BadKeyNotInAnnotations) {
  SetUpProductProvider(std::make_unique<stubs::ProductInfoProvider>(CreateProductInfo({
      {kAnnotationHardwareProductLanguage, "some-language"},
      {kAnnotationHardwareProductLocaleList, "some-locale1, some-locale2, some-locale3"},
      {kAnnotationHardwareProductManufacturer, "some-manufacturer"},
      {kAnnotationHardwareProductModel, "some-model"},
      {kAnnotationHardwareProductName, "some-name"},
      {kAnnotationHardwareProductRegulatoryDomain, "some-country-code"},
      {kAnnotationHardwareProductSKU, "some-sku"},
  })));

  auto product_info = GetProductInfo({
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
  SetUpProductProvider(std::make_unique<stubs::ProductInfoProvider>(CreateProductInfo({
      {kAnnotationHardwareProductLocaleList, "some-locale1, some-locale2, some-locale3"},
      {kAnnotationHardwareProductModel, "some-model"},
      {kAnnotationHardwareProductName, "some-name"},
      {kAnnotationHardwareProductSKU, "some-sku"},
  })));

  auto product_info = GetProductInfo({
      kAnnotationHardwareProductSKU,
      kAnnotationHardwareProductLanguage,
      kAnnotationHardwareProductRegulatoryDomain,
      kAnnotationHardwareProductLocaleList,
      kAnnotationHardwareProductName,
      kAnnotationHardwareProductModel,
      kAnnotationHardwareProductManufacturer,
  });

  EXPECT_THAT(
      product_info,
      ElementsAreArray({
          Pair(kAnnotationHardwareProductLocaleList, "some-locale1, some-locale2, some-locale3"),
          Pair(kAnnotationHardwareProductModel, "some-model"),
          Pair(kAnnotationHardwareProductName, "some-name"),
          Pair(kAnnotationHardwareProductSKU, "some-sku"),
      }));
}

TEST_F(ProductInfoProviderTest, Check_CobaltLogsTimeout) {
  SetUpProductProvider(std::make_unique<stubs::ProductInfoProviderNeverReturns>());

  auto board_info = GetProductInfo();

  ASSERT_TRUE(board_info.empty());
  EXPECT_THAT(ReceivedCobaltEvents(), ElementsAreArray({
                                          CobaltEvent(TimedOutData::kProductInfo),
                                      }));
}

TEST_F(ProductInfoProviderTest, Fail_CallGetProductInfoTwice) {
  SetUpProductProvider(std::make_unique<stubs::ProductInfoProvider>(CreateProductInfo({})));
  SetUpCobaltLoggerFactory(std::make_unique<stubs::CobaltLoggerFactory>());
  Cobalt cobalt(dispatcher(), services());

  const zx::duration unused_timeout = zx::sec(1);
  internal::ProductInfoPtr product_info_ptr(dispatcher(), services(), &cobalt);
  executor_.schedule_task(product_info_ptr.GetProductInfo(unused_timeout));
  ASSERT_DEATH(product_info_ptr.GetProductInfo(unused_timeout),
               testing::HasSubstr("GetProductInfo() is not intended to be called twice"));
}

const Annotations ProductInfoValues = {
    {kAnnotationHardwareProductSKU, "some-sku"},
    {kAnnotationHardwareProductLanguage, "some-language"},
    {kAnnotationHardwareProductRegulatoryDomain, "some-country-code"},
    {kAnnotationHardwareProductLocaleList, "some-locale1, some-locale2, some-locale3"},
    {kAnnotationHardwareProductName, "some-name"},
    {kAnnotationHardwareProductModel, "some-model"},
    {kAnnotationHardwareProductManufacturer, "some-manufacturer"},
};

std::vector<Annotations> GetProductInfoMapsWithOneKey() {
  std::vector<Annotations> maps;

  for (const auto& [key, value] : ProductInfoValues) {
    maps.push_back({{key, value}});
  }

  return maps;
}

std::vector<Annotations> GetProductInfosToTest() {
  auto maps = GetProductInfoMapsWithOneKey();
  maps.push_back(ProductInfoValues);
  return maps;
}

// Return all of the strings after the last '.' in each key concatenated together in camelCase.
std::string GetTestCaseName(const testing::TestParamInfo<Annotations>& info) {
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
  Annotations annotations = GetParam();
  SetUpProductProvider(
      std::make_unique<stubs::ProductInfoProvider>(CreateProductInfo(annotations)));

  AnnotationKeys keys;
  for (const auto& [key, _] : annotations) {
    keys.insert(key);
  }

  auto product_info = GetProductInfo(keys);
  EXPECT_EQ(product_info.size(), annotations.size());
  for (const auto& [key, value] : annotations) {
    EXPECT_EQ(product_info[key], value);
  }
}

}  // namespace
}  // namespace feedback
