// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_INFO_STORE_INFO_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_INFO_STORE_INFO_H_

#include <memory>

#include "src/developer/forensics/crash_reports/info/info_context.h"

namespace forensics {
namespace crash_reports {

class StoreInfo {
 public:
  StoreInfo(std::shared_ptr<InfoContext> context);

  void LogMaxStoreSize(StorageSize max_size);
  void LogGarbageCollection(size_t num_reports);

 private:
  std::shared_ptr<InfoContext> context_;
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_INFO_STORE_INFO_H_
