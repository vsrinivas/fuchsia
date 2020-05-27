// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crash_reports/crash_register.h"

#include <lib/syslog/cpp/macros.h>

#include <optional>

#include "garnet/public/lib/fostr/fidl/fuchsia/feedback/formatting.h"
#include "src/developer/feedback/utils/errors.h"

namespace feedback {

CrashRegister::CrashRegister(std::shared_ptr<InfoContext> info_context)
    : info_(std::move(info_context)) {}

namespace {

Product ToInternalProduct(const fuchsia::feedback::CrashReportingProduct& fidl_product) {
  FX_CHECK(fidl_product.has_name());
  return {fidl_product.name(),
          fidl_product.has_version() ? ErrorOr<std::string>(fidl_product.version())
                                     : ErrorOr<std::string>(Error::kMissingValue),
          fidl_product.has_channel() ? ErrorOr<std::string>(fidl_product.channel())
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

}  // namespace feedback
