// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/annotations/product_info_provider.h"

#include <fuchsia/intl/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

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
  const AnnotationKeys annotations_to_get = RestrictAllowlist(allowlist, kSupportedAnnotations);
  if (annotations_to_get.empty()) {
    return ::fpromise::make_result_promise<Annotations>(::fpromise::ok<Annotations>({}));
  }

  return product_ptr_
      .GetValue(fit::Timeout(
          timeout,
          /*action*/ [=] { cobalt_->LogOccurrence(cobalt::TimedOutData::kProductInfo); }))
      .then([=](const ::fpromise::result<std::map<AnnotationKey, std::string>, Error>& result) {
        Annotations annotations;

        if (result.is_error()) {
          for (const auto& key : annotations_to_get) {
            annotations.insert({key, result.error()});
          }
        } else {
          for (const auto& key : annotations_to_get) {
            const auto& product_info = result.value();
            if (product_info.find(key) == product_info.end()) {
              annotations.insert({key, Error::kMissingValue});
            } else {
              annotations.insert({key, product_info.at(key)});
            }
          }
        }
        return ::fpromise::ok(std::move(annotations));
      });
}

namespace {

using fuchsia::intl::LocaleId;
using fuchsia::intl::RegulatoryDomain;

std::optional<std::string> ExtractCountryCode(const RegulatoryDomain& regulatory_domain) {
  if (regulatory_domain.has_country_code()) {
    return regulatory_domain.country_code();
  }
  return std::nullopt;
}

// Convert the list of |LocaleId| into a string of comma separated values.
std::optional<std::string> Join(const std::vector<LocaleId>& locale_list) {
  if (locale_list.empty()) {
    return std::nullopt;
  }

  std::vector<std::string> locale_ids;
  for (const auto& local_id : locale_list) {
    locale_ids.push_back(local_id.id);
  }

  return fxl::JoinStrings(locale_ids, ", ");
}

}  // namespace

void ProductInfoProvider::GetInfo() {
  product_ptr_->GetInfo([this](ProductInfo info) {
    std::map<AnnotationKey, std::string> product_info;

    if (info.has_sku()) {
      product_info[kAnnotationHardwareProductSKU] = info.sku();
    }

    if (info.has_language()) {
      product_info[kAnnotationHardwareProductLanguage] = info.language();
    }

    if (info.has_regulatory_domain()) {
      const auto regulatory_domain = ExtractCountryCode(info.regulatory_domain());
      if (regulatory_domain) {
        product_info[kAnnotationHardwareProductRegulatoryDomain] = regulatory_domain.value();
      }
    }

    if (info.has_locale_list()) {
      const auto locale_list = Join(info.locale_list());
      if (locale_list) {
        product_info[kAnnotationHardwareProductLocaleList] = locale_list.value();
      }
    }

    if (info.has_name()) {
      product_info[kAnnotationHardwareProductName] = info.name();
    }

    if (info.has_model()) {
      product_info[kAnnotationHardwareProductModel] = info.model();
    }

    if (info.has_manufacturer()) {
      product_info[kAnnotationHardwareProductManufacturer] = info.manufacturer();
    }

    product_ptr_.SetValue(product_info);
  });
}

}  // namespace feedback_data
}  // namespace forensics
