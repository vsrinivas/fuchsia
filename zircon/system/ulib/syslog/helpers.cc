// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helpers.h"

#include <cstring>

namespace syslog {
namespace internal {

const char* StripDots(const char* path) {
  while (strncmp(path, "../", 3) == 0) {
    path += 3;
  }
  return path;
}

const char* StripPath(const char* path) {
  auto p = strrchr(path, '/');
  if (p) {
    return p + 1;
  } else {
    return path;
  }
}

const char* StripFile(const char* file, fx_log_severity_t severity) {
  return severity > FX_LOG_INFO ? StripDots(file) : StripPath(file);
}

}  // namespace internal
}  // namespace syslog
