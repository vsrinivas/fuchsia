// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/crash_register.h"

#include <lib/fit/result.h>
#include <lib/syslog/cpp/macros.h>

#include "garnet/public/lib/fostr/fidl/fuchsia/feedback/formatting.h"
#include "src/developer/forensics/utils/fidl/channel_provider_ptr.h"
#include "src/developer/forensics/utils/fit/timeout.h"

namespace forensics {
namespace crash_reports {

CrashRegister::CrashRegister(async_dispatcher_t* dispatcher,
                             std::shared_ptr<sys::ServiceDirectory> services,
                             std::shared_ptr<InfoContext> info_context,
                             const ErrorOr<std::string>& build_version)
    : dispatcher_(dispatcher),
      services_(services),
      info_(std::move(info_context)),
      build_version_(build_version) {}

namespace {

Product ToInternalProduct(const fuchsia::feedback::CrashReportingProduct& fidl_product) {
  FX_CHECK(fidl_product.has_name());
  return {.name = fidl_product.name(),
          .version = fidl_product.has_version() ? ErrorOr<std::string>(fidl_product.version())
                                                : ErrorOr<std::string>(Error::kMissingValue),
          .channel = fidl_product.has_channel() ? ErrorOr<std::string>(fidl_product.channel())
                                                : ErrorOr<std::string>(Error::kMissingValue)};
}

}  // namespace

void CrashRegister::Upsert(std::string component_url,
                           fuchsia::feedback::CrashReportingProduct product) {
  if (!product.has_name()) {
    FX_LOGS(WARNING) << "Missing required name in product:" << product;
    return;
  }

  const Product internal_product = ToInternalProduct(product);
  info_.UpsertComponentToProductMapping(component_url, internal_product);
  component_to_products_.insert_or_assign(component_url, std::move(internal_product));
}

::fit::promise<Product> CrashRegister::GetProduct(const std::string& program_name,
                                                  fit::Timeout timeout) {
  if (component_to_products_.find(program_name) != component_to_products_.end()) {
    return ::fit::make_result_promise<Product>(
        ::fit::ok<Product>(component_to_products_.at(program_name)));
  }

  return fidl::GetCurrentChannel(dispatcher_, services_, std::move(timeout))
      .then([build_version = build_version_](::fit::result<std::string, Error>& result) {
        return ::fit::ok<Product>({.name = "Fuchsia",
                                   .version = build_version,
                                   .channel = result.is_ok()
                                                  ? ErrorOr<std::string>(result.value())
                                                  : ErrorOr<std::string>(result.error())});
      });
}

}  // namespace crash_reports
}  // namespace forensics
