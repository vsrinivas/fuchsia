// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/annotations/product_info_provider.h"

#include <fuchsia/intl/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <zircon/errors.h>

#include <optional>
#include <string>

#include "src/developer/feedback/feedback_agent/annotations/aliases.h"
#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/developer/feedback/utils/promise.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace {

using fuchsia::hwinfo::ProductInfo;

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

ProductInfoProvider::ProductInfoProvider(const AnnotationKeys& annotations_to_get,
                                         async_dispatcher_t* dispatcher,
                                         std::shared_ptr<sys::ServiceDirectory> services,
                                         zx::duration timeout, Cobalt* cobalt)
    : annotations_to_get_(annotations_to_get),
      dispatcher_(dispatcher),
      services_(services),
      timeout_(timeout),
      cobalt_(cobalt) {
  const auto supported_annotations = GetSupportedAnnotations();
  AnnotationKeys not_supported;
  for (const auto& annotation : annotations_to_get_) {
    if (supported_annotations.find(annotation) == supported_annotations.end()) {
      FX_LOGS(WARNING) << "annotation " << annotation << " not supported by ProductInfoProvider";
      not_supported.insert(annotation);
    }
  }

  for (auto annotation : not_supported) {
    annotations_to_get_.erase(annotation);
  }
}

AnnotationKeys ProductInfoProvider::GetSupportedAnnotations() {
  return {
      kAnnotationHardwareProductSKU,
      kAnnotationHardwareProductLanguage,
      kAnnotationHardwareProductRegulatoryDomain,
      kAnnotationHardwareProductLocaleList,
      kAnnotationHardwareProductName,
      kAnnotationHardwareProductModel,
      kAnnotationHardwareProductManufacturer,
  };
}

fit::promise<Annotations> ProductInfoProvider::GetAnnotations() {
  auto product_info_ptr =
      std::make_unique<internal::ProductInfoPtr>(dispatcher_, services_, cobalt_);

  auto product_info = product_info_ptr->GetProductInfo(timeout_);

  return ExtendArgsLifetimeBeyondPromise(std::move(product_info),
                                         /*args=*/std::move(product_info_ptr))
      .and_then([annotations_to_get = annotations_to_get_](const Annotations& product_info) {
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

        return fit::ok(std::move(annotations));
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
                               std::shared_ptr<sys::ServiceDirectory> services, Cobalt* cobalt)
    : services_(services),
      cobalt_(cobalt),
      bridge_(dispatcher, "Hardware product info retrieval") {}

fit::promise<Annotations> ProductInfoPtr::GetProductInfo(zx::duration timeout) {
  FXL_CHECK(!has_called_get_product_info_) << "GetProductInfo() is not intended to be called twice";
  has_called_get_product_info_ = true;

  product_ptr_ = services_->Connect<fuchsia::hwinfo::Product>();

  product_ptr_.set_error_handler([this](zx_status_t status) {
    if (bridge_.IsAlreadyDone()) {
      return;
    }

    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.hwinfo.Product";

    bridge_.CompleteError();
  });

  product_ptr_->GetInfo([this](ProductInfo info) {
    if (bridge_.IsAlreadyDone()) {
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

    bridge_.CompleteOk(std::move(product_info));
  });

  return bridge_.WaitForDone(
      timeout, /*if_timeout=*/[this] { cobalt_->LogOccurrence(TimedOutData::kProductInfo); });
}

}  // namespace internal
}  // namespace feedback
