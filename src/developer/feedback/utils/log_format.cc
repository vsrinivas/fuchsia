// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/log_format.h"

#include <cinttypes>

#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace {

std::string SeverityToString(const int32_t severity) {
  if (severity < 0) {
    return fxl::StringPrintf("VLOG(%d)", -severity);
  } else if (severity == FX_LOG_INFO) {
    return "INFO";
  } else if (severity == FX_LOG_WARNING) {
    return "WARN";
  } else if (severity == FX_LOG_ERROR) {
    return "ERROR";
  } else if (severity == FX_LOG_FATAL) {
    return "FATAL";
  }
  return "INVALID";
}

}  // namespace

std::string Format(const fuchsia::logger::LogMessage& message) {
  return fxl::StringPrintf("[%05d.%03d][%05" PRIu64 "][%05" PRIu64 "][%s] %s: %s\n",
                           static_cast<int>(message.time / 1000000000ULL),
                           static_cast<int>((message.time / 1000000ULL) % 1000ULL), message.pid,
                           message.tid, fxl::JoinStrings(message.tags, ", ").c_str(),
                           SeverityToString(message.severity).c_str(), message.msg.c_str());
}

}  // namespace feedback
