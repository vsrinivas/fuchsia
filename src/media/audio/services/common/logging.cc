// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/common/logging.h"

#include <lib/zx/clock.h>

#include <algorithm>
#include <optional>

namespace media_audio {

namespace {

class ThrottledLoggerFromCounts : public ThrottledLogger {
 public:
  explicit ThrottledLoggerFromCounts(std::vector<std::pair<syslog::LogSeverity, int64_t>> counts)
      : counts_per_severity_(std::move(counts)) {
    // Sort with higest severity first.
    std::sort(counts_per_severity_.begin(), counts_per_severity_.end(),
              [](auto& a, auto& b) { return a.first > b.second; });
  }

  bool next_enabled() final {
    ++count_;
    for (auto [severity, period] : counts_per_severity_) {
      if (period % count_ == 0) {
        current_severity_ = severity;
        return true;
      }
    }
    current_severity_ = std::nullopt;
    return false;
  }

  syslog::LogSeverity current_severity() final {
    FX_CHECK(current_severity_);
    return *current_severity_;
  }

 private:
  int64_t count_{-1};
  std::vector<std::pair<syslog::LogSeverity, int64_t>> counts_per_severity_;
  std::optional<syslog::LogSeverity> current_severity_;
};

}  // namespace

// static
std::unique_ptr<ThrottledLogger> ThrottledLogger::FromCounts(
    std::vector<std::pair<syslog::LogSeverity, int64_t>> counts) {
  return std::make_unique<ThrottledLoggerFromCounts>(std::move(counts));
}

}  // namespace media_audio
