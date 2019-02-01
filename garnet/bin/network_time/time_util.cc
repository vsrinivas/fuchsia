// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "time_util.h"
#include <string>

namespace time_server {

std::string ToIso8601String(time_t epoch_seconds) {
  return ToIso8601String(gmtime(&epoch_seconds));
}

std::string ToIso8601String(struct tm* tm) {
  char buffer[sizeof("2018-09-21T12:34:56Z")];
  strftime(buffer, sizeof(buffer), "%FT%TZ", tm);
  return std::string(buffer);
}

}  // namespace time_server
