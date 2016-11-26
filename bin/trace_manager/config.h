// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_TRACE_MANAGER_CONFIG_H_
#define APPS_TRACING_SRC_TRACE_MANAGER_CONFIG_H_

#include <map>
#include <string>

#include "lib/ftl/macros.h"

namespace tracing {

class Config {
 public:
  Config();
  ~Config();

  // Tries to parse configuration from |command_line|.
  // Exits the process with error in case of issues.
  bool ReadFrom(const std::string& config_file);

  // All categories known to the |TraceManager|, with every
  // category being described by a short string.
  std::map<std::string, std::string> known_categories() const {
    return known_categories_;
  }

 private:
  std::map<std::string, std::string> known_categories_;
};

}  // namespace tracing

#endif  // APPS_TRACING_SRC_TRACE_MANAGER_CONFIG_H_
