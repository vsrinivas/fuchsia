// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/annotations/product_info_provider.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/hwinfo/cpp/fidl.h>
#include <fuchsia/intl/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <map>
#include <memory>
#include <string>

#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/developer/feedback/feedback_agent/tests/stub_product.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/split_string.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

using fuchsia::feedback::Annotation;
using fuchsia::hwinfo::ProductInfo;
using fuchsia::intl::LocaleId;
using fuchsia::intl::RegulatoryDomain;
using fxl::SplitResult::kSplitWantNonEmpty;
using fxl::WhiteSpaceHandling::kTrimWhitespace;
using sys::testing::ServiceDirectoryProvider;
using testing::Pair;
using testing::UnorderedElementsAreArray;

class ProductInfoProviderTest
    : public UnitTestFixture,
      public testing::WithParamInterface<std::map<std::string, std::string>> {
 public:
  ProductInfoProviderTest() : executor_(dispatcher()) {}

 protected:
  void SetUpProductProvider(std::unique_ptr<StubProduct> product_provider) {
    product_provider_ = std::move(product_provider);
    if (product_provider_) {
      InjectServiceProvider(product_provider_.get());
    }
  }

  std::map<std::string, std::string> GetProductInfo(const std::set<std::string>& annotations_to_get,
                                                    const zx::duration timeout = zx::sec(1)) {
    ProductInfoProvider provider(annotations_to_get, dispatcher(), services(), timeout);

    auto promise = provider.GetAnnotations();

    std::vector<Annotation> annotations;
    executor_.schedule_task(
        std::move(promise).then([&annotations](fit::result<std::vector<Annotation>>& res) {
          if (res.is_ok()) {
            annotations = res.take_value();
          }
        }));
    RunLoopFor(timeout);

    if (annotations.empty()) {
      return {};
    }

    std::map<std::string, std::string> product_info;
    for (auto& annotation : annotations) {
      product_info[annotation.key] = std::move(annotation.value);
    }

    return product_info;
  }

  async::Executor executor_;

 private:
  std::unique_ptr<StubProduct> product_provider_;
};

ProductInfo CreateProductInfo(const std::map<std::string, std::string>& annotations) {
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
  SetUpProductProvider(std::make_unique<StubProduct>(CreateProductInfo({
      {kAnnotationHardwareProductSKU, "some-sku"},
      {kAnnotationHardwareProductLanguage, "some-language"},
      {kAnnotationHardwareProductRegulatoryDomain, "some-country-code"},
      {kAnnotationHardwareProductLocaleList, "some-locale1, some-locale2, some-locale3"},
      {kAnnotationHardwareProductName, "some-name"},
      {kAnnotationHardwareProductModel, "some-model"},
      {kAnnotationHardwareProductManufacturer, "some-manufacturer"},
  })));

  auto product_info = GetProductInfo({
      kAnnotationHardwareProductSKU,
      kAnnotationHardwareProductModel,
  });
  EXPECT_THAT(product_info, UnorderedElementsAreArray({
                                Pair(kAnnotationHardwareProductSKU, "some-sku"),
                                Pair(kAnnotationHardwareProductModel, "some-model"),
                            }));
}

TEST_F(ProductInfoProviderTest, Check_BadKeyNotInAnnotations) {
  SetUpProductProvider(std::make_unique<StubProduct>(CreateProductInfo({
      {kAnnotationHardwareProductSKU, "some-sku"},
      {kAnnotationHardwareProductLanguage, "some-language"},
      {kAnnotationHardwareProductRegulatoryDomain, "some-country-code"},
      {kAnnotationHardwareProductLocaleList, "some-locale1, some-locale2, some-locale3"},
      {kAnnotationHardwareProductName, "some-name"},
      {kAnnotationHardwareProductModel, "some-model"},
      {kAnnotationHardwareProductManufacturer, "some-manufacturer"},
  })));

  auto product_info = GetProductInfo({
      kAnnotationHardwareProductSKU,
      kAnnotationHardwareProductModel,
      "bad_annotation",
  });

  EXPECT_THAT(product_info, UnorderedElementsAreArray({
                                Pair(kAnnotationHardwareProductSKU, "some-sku"),
                                Pair(kAnnotationHardwareProductModel, "some-model"),
                            }));
}

TEST_F(ProductInfoProviderTest, Succeed_ProductInfoReturnsFewerAnnotations) {
  SetUpProductProvider(std::make_unique<StubProduct>(CreateProductInfo({
      {kAnnotationHardwareProductSKU, "some-sku"},
      {kAnnotationHardwareProductModel, "some-model"},
      {kAnnotationHardwareProductName, "some-name"},
      {kAnnotationHardwareProductLocaleList, "some-locale1, some-locale2, some-locale3"},
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
      UnorderedElementsAreArray({
          Pair(kAnnotationHardwareProductSKU, "some-sku"),
          Pair(kAnnotationHardwareProductModel, "some-model"),
          Pair(kAnnotationHardwareProductName, "some-name"),
          Pair(kAnnotationHardwareProductLocaleList, "some-locale1, some-locale2, some-locale3"),
      }));
}

TEST_F(ProductInfoProviderTest, Fail_CallGetProductInfoTwice) {
  SetUpProductProvider(std::make_unique<StubProduct>(CreateProductInfo({})));

  const zx::duration unused_timeout = zx::sec(1);
  internal::ProductInfoPtr product_info_ptr(dispatcher(), services());
  executor_.schedule_task(product_info_ptr.GetProductInfo(unused_timeout));
  ASSERT_DEATH(product_info_ptr.GetProductInfo(unused_timeout),
               testing::HasSubstr("GetProductInfo() is not intended to be called twice"));
}

const std::map<std::string, std::string> ProductInfoValues = {
    {kAnnotationHardwareProductSKU, "some-sku"},
    {kAnnotationHardwareProductLanguage, "some-language"},
    {kAnnotationHardwareProductRegulatoryDomain, "some-country-code"},
    {kAnnotationHardwareProductLocaleList, "some-locale1, some-locale2, some-locale3"},
    {kAnnotationHardwareProductName, "some-name"},
    {kAnnotationHardwareProductModel, "some-model"},
    {kAnnotationHardwareProductManufacturer, "some-manufacturer"},
};

std::vector<std::map<std::string, std::string>> GetProductInfoMapsWithOneKey() {
  std::vector<std::map<std::string, std::string>> maps;

  for (const auto& [key, value] : ProductInfoValues) {
    maps.push_back({{key, value}});
  }

  return maps;
}

std::vector<std::map<std::string, std::string>> GetProductInfosToTest() {
  auto maps = GetProductInfoMapsWithOneKey();
  maps.push_back(ProductInfoValues);
  return maps;
}

// Return all of the strings after the last '.' in each key concatenated together in camelCase.
std::string GetTestCaseName(
    const testing::TestParamInfo<std::map<std::string, std::string>>& info) {
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
  std::map<std::string, std::string> annotations = GetParam();
  SetUpProductProvider(std::make_unique<StubProduct>(CreateProductInfo(annotations)));

  std::set<std::string> keys;
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
