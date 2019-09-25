// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/report_annotations.h"

#include <lib/syslog/cpp/logger.h>

#include <string>

#include "src/developer/feedback/crashpad_agent/crash_report_util.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/trim.h"

namespace feedback {
namespace {

std::string ReadStringFromFile(const std::string& filepath) {
  std::string content;
  if (!files::ReadFileToString(filepath, &content)) {
    FX_LOGS(ERROR) << "Failed to read content from '" << filepath << "'.";
    return "unknown";
  }
  return fxl::TrimString(content, "\r\n").ToString();
}

std::map<std::string, std::string> MakeCrashServerAnnotations(
    const fuchsia::feedback::CrashReport& report, const fuchsia::feedback::Data& feedback_data,
    const bool has_minidump) {
  std::map<std::string, std::string> annotations = {
      {"product", "Fuchsia"},
      {"version", ReadStringFromFile("/config/build-info/version")},
      // We use ptype to benefit from Chrome's "Process type" handling in the crash server UI.
      {"ptype", report.program_name()},
      {"osName", "Fuchsia"},
      {"osVersion", "0.0.0"},
      // Only the minidump file needs to be processed by the crash server. Reports without a
      // minidump should not have their file attachments processed.
      {"should_process", has_minidump ? "true" : "false"},
  };

  if (feedback_data.has_annotations()) {
    for (const auto& annotation : feedback_data.annotations()) {
      annotations[annotation.key] = annotation.value;
    }
  }

  return annotations;
}

}  // namespace

std::map<std::string, std::string> BuildAnnotations(const fuchsia::feedback::CrashReport& report,
                                                    const fuchsia::feedback::Data& feedback_data,
                                                    const bool has_minidump) {
  // Crash server annotations common to all crash reports.
  std::map<std::string, std::string> annotations =
      MakeCrashServerAnnotations(report, feedback_data, has_minidump);

  // Optional annotations filled by the client.
  ExtractAnnotations(report, &annotations);

  return annotations;
}

}  // namespace feedback
