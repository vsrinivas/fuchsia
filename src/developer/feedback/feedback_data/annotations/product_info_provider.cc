// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_data/annotations/product_info_provider.h"

#include <fuchsia/intl/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <zircon/errors.h>

#include <optional>
#include <string>

#include "src/developer/feedback/feedback_data/annotations/aliases.h"
#include "src/developer/feedback/feedback_data/annotations/utils.h"
#include "src/developer/feedback/feedback_data/constants.h"
#include "src/developer/feedback/utils/fit/promise.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
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

// Required annotations as per /src/hwinfo/hwinfo_product_config_schema.json.
bool IsRequired(const AnnotationKey& annotation) {
  static const AnnotationKeys required_annotations = {
      kAnnotationHardwareProductName,
      kAnnotationHardwareProductModel,
      kAnnotationHardwareProductManufacturer,
  };

  return required_annotations.find(annotation) != required_annotations.end();
}

}  // namespace

ProductInfoProvider::ProductInfoProvider(async_dispatcher_t* dispatcher,
                                         std::shared_ptr<sys::ServiceDirectory> services,
                                         zx::duration timeout, cobalt::Logger* cobalt)
    : dispatcher_(dispatcher), services_(services), timeout_(timeout), cobalt_(cobalt) {}

::fit::promise<Annotations> ProductInfoProvider::GetAnnotations(const AnnotationKeys& allowlist) {
  const AnnotationKeys annotations_to_get = RestrictAllowlist(allowlist, kSupportedAnnotations);
  if (annotations_to_get.empty()) {
    return ::fit::make_result_promise<Annotations>(::fit::ok<Annotations>({}));
  }

  auto product_info_ptr = std::make_unique<internal::ProductInfoPtr>(dispatcher_, services_);

  auto product_info = product_info_ptr->GetProductInfo(fit::Timeout(
      timeout_, /*action=*/[=] { cobalt_->LogOccurrence(cobalt::TimedOutData::kProductInfo); }));

  return fit::ExtendArgsLifetimeBeyondPromise(std::move(product_info),
                                              /*args=*/std::move(product_info_ptr))
      .and_then([=](const Annotations& product_info) {
        Annotations annotations;

        for (const auto& key : annotations_to_get) {
          if (product_info.find(key) == product_info.end()) {
            if (IsRequired(key)) {
              FX_LOGS(WARNING) << "Failed to build annotation " << key;
            }
            continue;
          }
          annotations[key] = product_info.at(key);
        }

        return ::fit::ok(std::move(annotations));
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

namespace internal {

ProductInfoPtr::ProductInfoPtr(async_dispatcher_t* dispatcher,
                               std::shared_ptr<sys::ServiceDirectory> services)
    : product_ptr_(dispatcher, services) {}

::fit::promise<Annotations> ProductInfoPtr::GetProductInfo(fit::Timeout timeout) {
  product_ptr_->GetInfo([this](ProductInfo info) {
    if (product_ptr_.IsAlreadyDone()) {
      return;
    }

    Annotations product_info;

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

    product_ptr_.CompleteOk(std::move(product_info));
  });

  return product_ptr_.WaitForDone(std::move(timeout));
}

}  // namespace internal
}  // namespace feedback
