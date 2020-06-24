// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_PRODUCT_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_PRODUCT_H_

#include <string>

#include "src/developer/forensics/utils/errors.h"

namespace forensics {
namespace crash_reports {

// Crash server product associated with the crash report.
struct Product {
  std::string name;
  ErrorOr<std::string> version;
  ErrorOr<std::string> channel;
};

bool operator==(const Product& a, const Product& b);

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_PRODUCT_H_
