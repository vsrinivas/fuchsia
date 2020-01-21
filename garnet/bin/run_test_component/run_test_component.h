// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_RUN_TEST_COMPONENT_RUN_TEST_COMPONENT_H_
#define GARNET_BIN_RUN_TEST_COMPONENT_RUN_TEST_COMPONENT_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>

#include <string>

namespace run {

struct ParseArgsResult {
  bool error;
  std::string error_msg;
  fuchsia::sys::LaunchInfo launch_info;
  std::vector<std::string> matching_urls;
  std::string realm_label;
};

// Parses args.
ParseArgsResult ParseArgs(const std::shared_ptr<sys::ServiceDirectory>& services, int argc,
                          const char** argv);

}  // namespace run

#endif  // GARNET_BIN_RUN_TEST_COMPONENT_RUN_TEST_COMPONENT_H_
