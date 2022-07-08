// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_CRASH_REGISTER_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_CRASH_REGISTER_H_

#include <fuchsia/feedback/cpp/fidl.h>

#include <map>
#include <memory>
#include <string>

#include "src/developer/forensics/crash_reports/annotation_map.h"
#include "src/developer/forensics/crash_reports/info/crash_register_info.h"
#include "src/developer/forensics/crash_reports/product.h"
#include "third_party/rapidjson/include/rapidjson/document.h"

namespace forensics {
namespace crash_reports {

class CrashRegister : public fuchsia::feedback::CrashReportingProductRegister {
 public:
  explicit CrashRegister(std::shared_ptr<InfoContext> info_context, std::string register_filepath);

  // |fuchsia::feedback::CrashReportingProductRegister|
  void Upsert(std::string component_url, fuchsia::feedback::CrashReportingProduct product) override;
  void UpsertWithAck(std::string component_url, fuchsia::feedback::CrashReportingProduct product,
                     UpsertWithAckCallback callback) override;

  bool HasProduct(const std::string& program_name) const;

  // Returns the Product registered by clients for a given component URL. Check-fails if non-exists.
  Product GetProduct(const std::string& program_name) const;

  // Adds the version and channel in |annotations| to |product|, if they exist.
  static void AddVersionAndChannel(Product& product, const AnnotationMap& annotations);

 private:
  void RestoreFromJson();
  void UpdateJson(const std::string& component_url, const Product& product);

  CrashRegisterInfo info_;

  std::map<std::string, Product> component_to_products_;

  rapidjson::Document register_json_;
  std::string register_filepath_;
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_CRASH_REGISTER_H_
