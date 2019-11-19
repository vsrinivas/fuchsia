// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/annotations/product_info_provider.h"

#include <fuchsia/intl/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fit/sequencer.h>
#include <zircon/errors.h>

#include <optional>

#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

using fuchsia::feedback::Annotation;
using fuchsia::hwinfo::ProductInfo;

ProductInfoProvider::ProductInfoProvider(const std::set<std::string>& annotations_to_get,
                                         async_dispatcher_t* dispatcher,
                                         std::shared_ptr<sys::ServiceDirectory> services,
                                         zx::duration timeout)
    : annotations_to_get_(annotations_to_get),
      dispatcher_(dispatcher),
      services_(services),
      timeout_(timeout) {}

std::set<std::string> ProductInfoProvider::GetSupportedAnnotations() {
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

fit::promise<std::vector<Annotation>> ProductInfoProvider::GetAnnotations() {
  std::vector<fit::promise<Annotation>> annotations;
  auto product_info_ptr = std::make_unique<internal::ProductInfoPtr>(dispatcher_, services_);

  return product_info_ptr->GetProductInfo(timeout_).and_then(
      [annotations_to_get = annotations_to_get_, product_info_ptr = std::move(product_info_ptr)](
          const std::map<std::string, std::string>& product_info) {
        std::vector<Annotation> annotations;

        for (const auto& key : annotations_to_get) {
          if (product_info.find(key) == product_info.end()) {
            FX_LOGS(WARNING) << "Failed to build annotation " << key;
            continue;
          }

          Annotation annotation;
          annotation.key = key;
          annotation.value = product_info.at(key);

          annotations.push_back(std::move(annotation));
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
                               std::shared_ptr<sys::ServiceDirectory> services)
    : dispatcher_(dispatcher), services_(services) {}

fit::promise<std::map<std::string, std::string>> ProductInfoPtr::GetProductInfo(
    zx::duration timeout) {
  FXL_CHECK(!has_called_get_product_info_) << "GetProductInfo() is not intended to be called twice";
  has_called_get_product_info_ = true;

  // fit::promise does not have the notion of a timeout. So we post a delayed task that will call
  // the completer after the timeout and return an error.
  //
  // We wrap the delayed task in a CancelableClosure so we can cancel it when the fit::bridge is
  // completed another way.
  //
  // It is safe to pass "this" to the fit::function as the callback won't be callable when the
  // CancelableClosure goes out of scope, which is before "this".
  done_after_timeout_.Reset([this] {
    if (!done_.completer) {
      return;
    }

    FX_LOGS(ERROR) << "Hardware product info retrieval timed out";
    done_.completer.complete_error();
  });

  product_ptr_ = services_->Connect<fuchsia::hwinfo::Product>();

  const zx_status_t post_status = async::PostDelayedTask(
      dispatcher_, [cb = done_after_timeout_.callback()] { cb(); }, timeout);
  if (post_status != ZX_OK) {
    FX_PLOGS(ERROR, post_status) << "Failed to post delayed task";
    FX_LOGS(ERROR)
        << "Skipping hardware product info retrieval as it is not safe without a timeout";
    return fit::make_result_promise<std::map<std::string, std::string>>(fit::error());
  }

  product_ptr_.set_error_handler([this](zx_status_t status) {
    if (!done_.completer) {
      return;
    }

    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.hwinfo.Product";

    done_.completer.complete_error();
  });

  product_ptr_->GetInfo([this](ProductInfo info) {
    std::map<std::string, std::string> product_info;

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

    done_.completer.complete_ok(std::move(product_info));
  });

  return done_.consumer.promise_or(fit::error())
      .then([this](fit::result<std::map<std::string, std::string>>& result) {
        done_after_timeout_.Cancel();
        return std::move(result);
      });
}

}  // namespace internal
}  // namespace feedback
