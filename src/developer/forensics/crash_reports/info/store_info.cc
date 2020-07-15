// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/info/store_info.h"

namespace forensics {
namespace crash_reports {

StoreInfo::StoreInfo(std::shared_ptr<InfoContext> context) : context_(std::move(context)) {}

void StoreInfo::LogMaxStoreSize(const StorageSize max_size) {
  context_->InspectManager().ExposeStore(max_size);
}

void StoreInfo::LogGarbageCollection(const size_t num_reports) {
  context_->InspectManager().IncreaseReportsGarbageCollectedBy(num_reports);
}

}  // namespace crash_reports
}  // namespace forensics
