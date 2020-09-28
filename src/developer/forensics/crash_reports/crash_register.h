// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_CRASH_REGISTER_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_CRASH_REGISTER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>

#include <map>
#include <memory>
#include <string>

#include "src/developer/forensics/crash_reports/info/crash_register_info.h"
#include "src/developer/forensics/crash_reports/info/info_context.h"
#include "src/developer/forensics/crash_reports/product.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fit/timeout.h"
// TODO(fxbug.dev/57392): Move it back to //third_party once unification completes.
#include "zircon/third_party/rapidjson/include/rapidjson/document.h"

namespace forensics {
namespace crash_reports {

class CrashRegister : public fuchsia::feedback::CrashReportingProductRegister {
 public:
  explicit CrashRegister(async_dispatcher_t* dispatcher,
                         std::shared_ptr<sys::ServiceDirectory> services,
                         std::shared_ptr<InfoContext> info_context,
                         const ErrorOr<std::string>& build_version, std::string register_filepath);

  // |fuchsia::feedback::CrashReportingProductRegister|
  void Upsert(std::string component_url, fuchsia::feedback::CrashReportingProduct product) override;

  // Returns the Product registered by clients for a given component URL, otherwise the default
  // Product for the platform.
  ::fit::promise<Product> GetProduct(const std::string& program_name, fit::Timeout timeout);

 private:
  void RestoreFromJson();
  void UpdateJson(const std::string& component_url, const Product& product);

  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  CrashRegisterInfo info_;
  const ErrorOr<std::string> build_version_;

  std::map<std::string, Product> component_to_products_;

  rapidjson::Document register_json_;
  std::string register_filepath_;
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_CRASH_REGISTER_H_
