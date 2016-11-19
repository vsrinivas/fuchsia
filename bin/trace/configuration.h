// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_TRACE_CONFIGURATION_H_
#define APPS_TRACING_SRC_TRACE_CONFIGURATION_H_

#include <string>
#include <utility>
#include <vector>

#include "apps/modular/services/application/application_launcher.fidl.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/time/time_delta.h"

namespace tracing {

struct Configuration {
  // Tries to parse configuration values from |command_line|.
  // Exits the process with error in case of issues.
  static Configuration ParseOrExit(const ftl::CommandLine& command_line);

  std::vector<std::string> categories = {};
  std::string output_file_name = "/tmp/trace.json";
  ftl::TimeDelta duration = ftl::TimeDelta::FromSeconds(10);
  bool list_providers = false;
  modular::ApplicationLaunchInfoPtr launch_info;
};

}  // namespace tracing

#endif  // APPS_TRACING_SRC_TRACE_CONFIGURATION_H_
