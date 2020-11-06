// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_PRODUCT_QUOTAS_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_PRODUCT_QUOTAS_H_

#include <lib/async/dispatcher.h>

#include <map>
#include <optional>
#include <string>

#include "src/developer/forensics/crash_reports/product.h"

namespace forensics {
namespace crash_reports {

// Maintains optional daily quota information for various different Products. Quotas are enforced on
// a per-version basis for each different product.
//
// If the quota is null, then operations on this class have no effect and a Product always has quota
// remaining.
class ProductQuotas {
 public:
  ProductQuotas(async_dispatcher_t* dispatcher, std::optional<uint64_t> quota);

  bool HasQuotaRemaining(const Product& product);
  void DecrementRemainingQuota(const Product& product);

 private:
  void Reset();

  async_dispatcher_t* dispatcher_;
  std::optional<uint64_t> quota_;
  std::map<std::string, uint64_t> remaining_quotas_;
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_PRODUCT_QUOTAS_H_
