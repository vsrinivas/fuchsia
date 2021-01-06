// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/testing/log_message.h"

#include <lib/zx/time.h>

#include <cstdint>

namespace forensics {
namespace testing {
namespace {

constexpr zx::duration kLogMessageBaseTimestamp = zx::sec(15604);
constexpr uint64_t kLogMessageProcessId = 7559;
constexpr uint64_t kLogMessageThreadId = 7687;

}  // namespace

fuchsia::logger::LogMessage BuildLogMessage(const int32_t severity, const std::string& text,
                                            const zx::duration timestamp_offset,
                                            const std::vector<std::string>& tags) {
  fuchsia::logger::LogMessage msg{};
  msg.time = (kLogMessageBaseTimestamp + timestamp_offset).get();
  msg.pid = kLogMessageProcessId;
  msg.tid = kLogMessageThreadId;
  msg.tags = tags;
  msg.severity = severity;
  msg.msg = text;
  return msg;
}

}  // namespace testing
}  // namespace forensics
