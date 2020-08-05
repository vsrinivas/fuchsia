// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_RUN_TEST_COMPONENT_RUN_TEST_COMPONENT_H_
#define GARNET_BIN_RUN_TEST_COMPONENT_RUN_TEST_COMPONENT_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fitx/result.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/logger.h>

#include <string>

namespace run {

struct ParseArgsResult {
  bool error;
  std::string error_msg;
  fuchsia::sys::LaunchInfo launch_info;
  std::vector<std::string> matching_urls;
  std::string realm_label;
  /// Timeout in seconds for test. By default there is no timeout.
  int32_t timeout = -1;
  int32_t min_log_severity = FX_LOG_TRACE;
  int32_t max_log_severity = FX_LOG_NONE;
};

// Parses args.
ParseArgsResult ParseArgs(const std::shared_ptr<sys::ServiceDirectory>& services, int argc,
                          const char** argv);

// Strips url of query parameters. For eg
//  "fuchsia-pkg://fuchsia.com/my-pkg?hash=hash#meta/my-component.cmx" will return
//  "fuchsia-pkg://fuchsia.com/my-pkg#meta/my-component.cmx".
std::string GetSimplifiedUrl(const std::string& url);

// Parse log level and return corresponding integer representation.
fitx::result<bool, uint32_t> ParseLogLevel(const std::string& level);

}  // namespace run

#endif  // GARNET_BIN_RUN_TEST_COMPONENT_RUN_TEST_COMPONENT_H_
