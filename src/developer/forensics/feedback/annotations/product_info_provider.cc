// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotations/product_info_provider.h"

#include <numeric>

#include "src/developer/forensics/feedback/annotations/constants.h"

namespace forensics::feedback {

Annotations ProductInfoToAnnotations::operator()(const fuchsia::hwinfo::ProductInfo& info) {
  Annotations annotations{
      {kHardwareProductSKUKey, Error::kMissingValue},
      {kHardwareProductLanguageKey, Error::kMissingValue},
      {kHardwareProductRegulatoryDomainKey, Error::kMissingValue},
      {kHardwareProductLocaleListKey, Error::kMissingValue},
      {kHardwareProductNameKey, Error::kMissingValue},
      {kHardwareProductModelKey, Error::kMissingValue},
      {kHardwareProductManufacturerKey, Error::kMissingValue},
  };

  if (info.has_sku()) {
    annotations.insert_or_assign(kHardwareProductSKUKey, info.sku());
  }

  if (info.has_language()) {
    annotations.insert_or_assign(kHardwareProductLanguageKey, info.language());
  }

  if (info.has_regulatory_domain() && info.regulatory_domain().has_country_code()) {
    annotations.insert_or_assign(kHardwareProductRegulatoryDomainKey,
                                 info.regulatory_domain().country_code());
  }

  if (info.has_locale_list() && !info.locale_list().empty()) {
    auto begin = std::begin(info.locale_list());
    auto end = std::end(info.locale_list());

    const std::string locale_list = std::accumulate(
        std::next(begin), end, begin->id,
        [](auto acc, const auto& locale) { return acc.append(", ").append(locale.id); });
    annotations.insert_or_assign(kHardwareProductLocaleListKey, locale_list);
  }

  if (info.has_name()) {
    annotations.insert_or_assign(kHardwareProductNameKey, info.name());
  }

  if (info.has_model()) {
    annotations.insert_or_assign(kHardwareProductModelKey, info.model());
  }

  if (info.has_manufacturer()) {
    annotations.insert_or_assign(kHardwareProductManufacturerKey, info.manufacturer());
  }

  return annotations;
}

std::set<std::string> ProductInfoProvider::GetKeys() const {
  return {
      kHardwareProductSKUKey,
      kHardwareProductLanguageKey,
      kHardwareProductRegulatoryDomainKey,
      kHardwareProductLocaleListKey,
      kHardwareProductNameKey,
      kHardwareProductModelKey,
      kHardwareProductManufacturerKey,
  };
}

}  // namespace forensics::feedback
