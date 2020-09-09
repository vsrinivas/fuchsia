// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_EXCEPTIONS_HANDLER_REPORT_BUILDER_H_
#define SRC_DEVELOPER_FORENSICS_EXCEPTIONS_HANDLER_REPORT_BUILDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/sys/internal/cpp/fidl.h>
#include <lib/zx/vmo.h>

#include <optional>
#include <string>

namespace forensics {
namespace exceptions {
namespace handler {

class CrashReportBuilder {
 public:
  CrashReportBuilder& SetProcessName(const std::string& process_name);
  CrashReportBuilder& SetMinidump(zx::vmo minidump);
  CrashReportBuilder& SetComponentInfo(
      const fuchsia::sys::internal::SourceIdentity& component_info);
  CrashReportBuilder& SetExceptionExpired();
  CrashReportBuilder& SetProcessTerminated();

  fuchsia::feedback::CrashReport Consume();

 private:
  std::optional<std::string> process_name_;
  std::optional<zx::vmo> minidump_{std::nullopt};
  std::optional<std::string> component_url_{std::nullopt};
  std::optional<std::string> realm_path_{std::nullopt};
  bool exception_expired_{false};
  bool process_already_terminated_{false};

  bool is_valid_{true};
};

}  // namespace handler
}  // namespace exceptions
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_EXCEPTIONS_HANDLER_REPORT_BUILDER_H_
