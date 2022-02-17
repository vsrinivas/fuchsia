// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/annotations/product_info_provider.h"

#include <fuchsia/intl/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

#include <numeric>
#include <optional>
#include <string>

#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/feedback_data/annotations/utils.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fit/promise.h"
#include "src/lib/fxl/strings/join_strings.h"

namespace forensics {
namespace feedback_data {
namespace {

using fuchsia::hwinfo::ProductInfo;

const AnnotationKeys kSupportedAnnotations = {
    kAnnotationHardwareProductSKU,
    kAnnotationHardwareProductLanguage,
    kAnnotationHardwareProductRegulatoryDomain,
    kAnnotationHardwareProductLocaleList,
    kAnnotationHardwareProductName,
    kAnnotationHardwareProductModel,
    kAnnotationHardwareProductManufacturer,
};

}  // namespace

ProductInfoProvider::ProductInfoProvider(async_dispatcher_t* dispatcher,
                                         std::shared_ptr<sys::ServiceDirectory> services,
                                         cobalt::Logger* cobalt)
    : dispatcher_(dispatcher),
      services_(services),
      cobalt_(cobalt),
      product_ptr_(dispatcher_, services_, [this] { GetInfo(); }) {}

::fpromise::promise<Annotations> ProductInfoProvider::GetAnnotations(
    zx::duration timeout, const AnnotationKeys& allowlist) {
  const AnnotationKeys to_get = RestrictAllowlist(allowlist, kSupportedAnnotations);
  if (to_get.empty()) {
    return ::fpromise::make_result_promise<Annotations>(::fpromise::ok<Annotations>({}));
  }

  auto on_timeout = [this] { cobalt_->LogOccurrence(cobalt::TimedOutData::kProductInfo); };
  return product_ptr_.GetValue(fit::Timeout(timeout, std::move(on_timeout)))
      .then([to_get](const ::fpromise::result<Annotations, Error>& result) {
        Annotations annotations = (result.is_error()) ? WithError(to_get, result.error())
                                                      : ExtractAllowlisted(to_get, result.value());
        return ::fpromise::ok(std::move(annotations));
      });
}

void ProductInfoProvider::GetInfo() {
  product_ptr_->GetInfo([this](ProductInfo info) {
    Annotations annotations({
        {kAnnotationHardwareProductSKU, Error::kMissingValue},
        {kAnnotationHardwareProductLanguage, Error::kMissingValue},
        {kAnnotationHardwareProductRegulatoryDomain, Error::kMissingValue},
        {kAnnotationHardwareProductLocaleList, Error::kMissingValue},
        {kAnnotationHardwareProductName, Error::kMissingValue},
        {kAnnotationHardwareProductModel, Error::kMissingValue},
        {kAnnotationHardwareProductManufacturer, Error::kMissingValue},
    });

    if (info.has_sku()) {
      annotations.insert_or_assign(kAnnotationHardwareProductSKU, info.sku());
    }

    if (info.has_language()) {
      annotations.insert_or_assign(kAnnotationHardwareProductLanguage, info.language());
    }

    if (info.has_regulatory_domain() && info.regulatory_domain().has_country_code()) {
      annotations.insert_or_assign(kAnnotationHardwareProductRegulatoryDomain,
                                   info.regulatory_domain().country_code());
    }

    if (info.has_locale_list() && !info.locale_list().empty()) {
      auto begin = std::begin(info.locale_list());
      auto end = std::end(info.locale_list());

      const std::string locale_list = std::accumulate(
          std::next(begin), end, begin->id,
          [](auto acc, const auto& locale) { return acc.append(", ").append(locale.id); });
      annotations.insert_or_assign(kAnnotationHardwareProductLocaleList, locale_list);
    }

    if (info.has_name()) {
      annotations.insert_or_assign(kAnnotationHardwareProductName, info.name());
    }

    if (info.has_model()) {
      annotations.insert_or_assign(kAnnotationHardwareProductModel, info.model());
    }

    if (info.has_manufacturer()) {
      annotations.insert_or_assign(kAnnotationHardwareProductManufacturer, info.manufacturer());
    }

    product_ptr_.SetValue(std::move(annotations));
  });
}

}  // namespace feedback_data
}  // namespace forensics
