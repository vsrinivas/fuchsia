// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/product_quotas.h"

#include <lib/async/cpp/task.h>
#include <lib/zx/time.h>

#include <optional>

#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace crash_reports {
namespace {

std::string Key(const Product& product) {
  if (product.version.HasValue()) {
    return fxl::StringPrintf("%s-%s", product.name.c_str(), product.version.Value().c_str());
  } else {
    return product.name;
  }
}

}  // namespace

ProductQuotas::ProductQuotas(async_dispatcher_t* dispatcher, const std::optional<uint64_t> quota)
    : dispatcher_(dispatcher), quota_(quota) {
  Reset();
}

void ProductQuotas::Reset() {
  // If no quota has been set, resetting is a no-op.
  if (!quota_.has_value()) {
    return;
  }

  remaining_quotas_.clear();
  async::PostDelayedTask(
      dispatcher_,
      [this] {
        FX_LOGS(INFO) << "Resetting quota for all products";
        Reset();
      },
      zx::hour(24));
}

bool ProductQuotas::HasQuotaRemaining(const Product& product) {
  // If no quota has been set, return true by default.
  if (!quota_.has_value()) {
    return true;
  }

  const auto key = Key(product);
  if (remaining_quotas_.find(key) == remaining_quotas_.end()) {
    remaining_quotas_[key] = quota_.value();
  }

  return remaining_quotas_[key] != 0u;
}

void ProductQuotas::DecrementRemainingQuota(const Product& product) {
  // If no quota has been set, there's nothing to decrement.
  if (!quota_.has_value()) {
    return;
  }

  const auto key = Key(product);
  FX_CHECK(remaining_quotas_.find(key) != remaining_quotas_.end());

  --(remaining_quotas_[key]);
}

}  // namespace crash_reports
}  // namespace forensics
