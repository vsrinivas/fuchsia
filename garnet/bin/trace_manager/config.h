// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_MANAGER_CONFIG_H_
#define GARNET_BIN_TRACE_MANAGER_CONFIG_H_

#include <map>
#include <string>

#include <fuchsia/sys/cpp/fidl.h>
#include "lib/fxl/macros.h"

namespace tracing {

class Config {
 public:
  Config();
  ~Config();

  // Tries to parse configuration from |command_line|.
  // Returns false if an error occurs.
  bool ReadFrom(const std::string& config_file);

  // All categories known to the |TraceManager|, with every
  // category being described by a short string.
  const std::map<std::string, std::string>& known_categories() const {
    return known_categories_;
  }

  // Well-known providers to start automatically.
  const std::map<std::string, fuchsia::sys::LaunchInfoPtr>& providers() const {
    return providers_;
  }

 private:
  std::map<std::string, std::string> known_categories_;
  std::map<std::string, fuchsia::sys::LaunchInfoPtr> providers_;
};

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_MANAGER_CONFIG_H_
