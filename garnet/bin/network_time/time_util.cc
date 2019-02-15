// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "time_util.h"
#include <string>

namespace time_server {

std::string ToIso8601String(time_t epoch_seconds) {
  return ToIso8601String(gmtime(&epoch_seconds));
}

std::string ToIso8601String(const struct tm* tm) {
  char buffer[sizeof("2018-09-21T12:34:56Z")];
  strftime(buffer, sizeof(buffer), "%FT%TZ", tm);
  return std::string(buffer);
}

const fuchsia::hardware::rtc::Time ToRtcTime(const struct tm* tm) {
  return fuchsia::hardware::rtc::Time{.seconds = uint8_t(tm->tm_sec),
                                      .minutes = uint8_t(tm->tm_min),
                                      .hours = uint8_t(tm->tm_hour),
                                      .day = uint8_t(tm->tm_mday),
                                      .month = uint8_t(tm->tm_mon + 1),
                                      .year = uint16_t(tm->tm_year + 1900)};
}

}  // namespace time_server
