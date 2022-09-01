// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_PRODUCT_QUOTAS_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_PRODUCT_QUOTAS_H_

#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/clock.h>

#include <map>
#include <optional>
#include <string>

#include "src/developer/forensics/crash_reports/product.h"
#include "src/developer/forensics/utils/utc_clock_ready_watcher.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/timekeeper/clock.h"
#include "third_party/rapidjson/include/rapidjson/document.h"

namespace forensics {
namespace crash_reports {

// Maintains optional daily quota information for various different Products. Quotas are enforced on
// a per-version basis for each different product.
//
// If the quota is null, then operations on this class have no effect and a Product always has quota
// remaining.
class ProductQuotas {
 public:
  ProductQuotas(async_dispatcher_t* dispatcher, timekeeper::Clock* clock,
                std::optional<uint64_t> quota, std::string quota_filepath,
                UtcClockReadyWatcherBase* utc_clock_ready_watcher);
  ProductQuotas(const ProductQuotas&) = delete;
  ProductQuotas(ProductQuotas&&) = delete;
  ProductQuotas& operator=(const ProductQuotas&) = delete;
  ProductQuotas& operator=(ProductQuotas&&) = delete;

  bool HasQuotaRemaining(const Product& product);
  void DecrementRemainingQuota(const Product& product);

 private:
  void Reset();
  void RestoreFromJson();
  void WriteJson();
  void UpdateJson(const std::string& key, uint64_t remaining_quota);
  void UpdateJson(timekeeper::time_utc next_reset_utc_time);

  // Keep waiting on the clock handle until the clock has started.
  void OnClockStart();

  async_dispatcher_t* dispatcher_;
  timekeeper::Clock* clock_;
  std::optional<uint64_t> quota_;
  const std::string quota_filepath_;
  UtcClockReadyWatcherBase* utc_clock_ready_watcher_;

  rapidjson::Document quota_json_;
  std::map<std::string, uint64_t> remaining_quotas_;

  // Should be exactly midnight UTC of a date, i.e. multiples of zx::hour(24). This is the value
  // currently saved in |quota_json_|.
  std::optional<timekeeper::time_utc> next_reset_utc_time_;
  async::TaskClosureMethod<ProductQuotas, &ProductQuotas::Reset> reset_task_{this};
  fxl::WeakPtrFactory<ProductQuotas> ptr_factory_{this};
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_PRODUCT_QUOTAS_H_
