// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_EXCEPTIONS_CRASH_REPORT_BUILDER_H_
#define SRC_DEVELOPER_FORENSICS_EXCEPTIONS_CRASH_REPORT_BUILDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/zx/vmo.h>

#include <optional>
#include <string>

namespace forensics {
namespace exceptions {

class CrashReportBuilder {
 public:
  CrashReportBuilder(const std::string& process_name) : process_name_(process_name) {}

  CrashReportBuilder& SetMinidump(zx::vmo minidump);
  CrashReportBuilder& SetComponentUrl(const std::string& component_url);
  CrashReportBuilder& SetRealmPath(const std::string& realm_path);

  fuchsia::feedback::CrashReport Consume();

 private:
  std::string process_name_;
  std::optional<zx::vmo> minidump_{std::nullopt};
  std::optional<std::string> component_url_{std::nullopt};
  std::optional<std::string> realm_path_{std::nullopt};

  bool is_valid_{true};
};

}  // namespace exceptions
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_EXCEPTIONS_CRASH_REPORT_BUILDER_H_
