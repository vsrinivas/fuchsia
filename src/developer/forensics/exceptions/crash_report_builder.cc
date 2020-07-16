// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/exceptions/crash_report_builder.h"

#include <lib/syslog/cpp/macros.h>

namespace forensics {
namespace exceptions {

CrashReportBuilder& CrashReportBuilder::SetProcessName(const std::string& process_name) {
  process_name_ = process_name;
  return *this;
}

CrashReportBuilder& CrashReportBuilder::SetMinidump(zx::vmo minidump) {
  minidump_ = std::move(minidump);
  return *this;
}

CrashReportBuilder& CrashReportBuilder::SetComponentUrl(const std::string& component_url) {
  component_url_ = component_url;
  return *this;
}

CrashReportBuilder& CrashReportBuilder::SetRealmPath(const std::string& realm_path) {
  realm_path_ = realm_path;
  return *this;
}

fuchsia::feedback::CrashReport CrashReportBuilder::Consume() {
  FX_CHECK(process_name_.has_value()) << "Need a process name";
  FX_CHECK(is_valid_) << "Consume can only be called once";
  is_valid_ = false;

  using namespace fuchsia::feedback;
  CrashReport crash_report;

  const std::string program_name =
      (component_url_.has_value()) ? component_url_.value() : process_name_.value();
  crash_report.set_program_name(program_name.substr(0, fuchsia::feedback::MAX_PROGRAM_NAME_LENGTH));

  crash_report.mutable_annotations()->push_back(Annotation{
      .key = "crash.process.name",
      .value = process_name_.value(),
  });

  if (!component_url_.has_value()) {
    crash_report.mutable_annotations()->push_back(Annotation{
        .key = "debug.crash.component.url.set",
        .value = "false",
    });
  }

  if (realm_path_.has_value()) {
    crash_report.mutable_annotations()->push_back(Annotation{
        .key = "crash.realm-path",
        .value = realm_path_.value(),
    });
  }

  NativeCrashReport native_crash_report;

  if (minidump_.has_value() && minidump_.value().is_valid()) {
    fuchsia::mem::Buffer mem_buffer;
    minidump_.value().get_size(&mem_buffer.size);
    mem_buffer.vmo = std::move(minidump_.value());

    native_crash_report.set_minidump(std::move(mem_buffer));
  }

  crash_report.set_specific_report(SpecificCrashReport::WithNative(std::move(native_crash_report)));

  minidump_ = std::nullopt;

  return crash_report;
}

}  // namespace exceptions
}  // namespace forensics
