// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASH_REPORTS_CRASH_REGISTER_H_
#define SRC_DEVELOPER_FEEDBACK_CRASH_REPORTS_CRASH_REGISTER_H_

#include <fuchsia/feedback/cpp/fidl.h>

#include <map>
#include <string>

#include "src/developer/feedback/crash_reports/info/crash_register_info.h"
#include "src/developer/feedback/crash_reports/info/info_context.h"
#include "src/developer/feedback/crash_reports/product.h"

namespace feedback {

class CrashRegister : public fuchsia::feedback::CrashReportingProductRegister {
 public:
  explicit CrashRegister(std::shared_ptr<InfoContext> info_context);

  // |fuchsia::feedback::CrashReportingProductRegister|
  void Upsert(std::string component_url, fuchsia::feedback::CrashReportingProduct product) override;

  // TODO(48451): expose the Product to use for a given program name, including a default product
  // for when no clients registered a Product for that component URL.

 private:
  CrashRegisterInfo info_;
  std::map<std::string, Product> component_to_products_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASH_REPORTS_CRASH_REGISTER_H_
