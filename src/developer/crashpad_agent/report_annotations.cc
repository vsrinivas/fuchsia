// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/crashpad_agent/report_annotations.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <string>

#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/zx/channel.h>
#include <zircon/boot/image.h>

#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/trim.h"

namespace fuchsia {
namespace crash {
namespace {

// The crash server expects specific key and values for some annotations in Dart
// crash reports.
const char kDartTypeKey[] = "type";
const char kDartTypeValue[] = "DartError";
const char kDartErrorMessageKey[] = "error_message";
const char kDartErrorRuntimeTypeKey[] = "error_runtime_type";

std::string ReadStringFromFile(const std::string& filepath) {
  std::string content;
  if (!files::ReadFileToString(filepath, &content)) {
    FX_LOGS(ERROR) << "Failed to read content from '" << filepath << "'.";
    return "unknown";
  }
  return fxl::TrimString(content, "\r\n").ToString();
}

}  // namespace

std::map<std::string, std::string> MakeDefaultAnnotations(
    const fuchsia::feedback::Data& feedback_data,
    const std::string& package_name) {
  std::map<std::string, std::string> annotations = {
      {"product", "Fuchsia"},
      {"version", ReadStringFromFile("/config/build-info/version")},
      // We use ptype to benefit from Chrome's "Process type" handling in
      // the crash server UI.
      {"ptype", package_name},
  };

  if (feedback_data.has_annotations()) {
    for (const auto& annotation : feedback_data.annotations()) {
      annotations[annotation.key] = annotation.value;
    }
  }

  return annotations;
}

std::map<std::string, std::string> MakeManagedRuntimeExceptionAnnotations(
    const fuchsia::feedback::Data& feedback_data,
    const std::string& component_url, ManagedRuntimeException* exception) {
  std::map<std::string, std::string> annotations =
      MakeDefaultAnnotations(feedback_data, component_url);
  switch (exception->Which()) {
    case ManagedRuntimeException::Tag::Invalid:
      FX_LOGS(ERROR) << "invalid ManagedRuntimeException";
      break;
    case ManagedRuntimeException::Tag::kUnknown_:
      // No additional annotations, just a single attachment.
      break;
    case ManagedRuntimeException::Tag::kDart:
      annotations[kDartTypeKey] = kDartTypeValue;
      annotations[kDartErrorRuntimeTypeKey] = std::string(
          reinterpret_cast<const char*>(exception->dart().type.data()));
      annotations[kDartErrorMessageKey] = std::string(
          reinterpret_cast<const char*>(exception->dart().message.data()));
      break;
  }
  return annotations;
}

}  // namespace crash
}  // namespace fuchsia
