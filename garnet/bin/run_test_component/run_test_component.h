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
  std::string cmx_file_path;
  std::vector<std::string> matching_urls;
};

// Parses fuchsia pkg url and returns cmx file path.
std::string GetComponentManifestPath(const std::string& url);

// Generates component url from cmx file path.
//
// This assumes that |cmx_file_path| would be a valid realtive path and will
// conform to pattern:
// <package_name>/0/meta/<cmx_file>.cmx
std::string GenerateComponentUrl(const std::string& cmx_file_path);

// Parses args.
ParseArgsResult ParseArgs(
    const std::shared_ptr<sys::ServiceDirectory>& services, int argc,
    const char** argv);

}  // namespace run

#endif  // GARNET_BIN_RUN_TEST_COMPONENT_RUN_TEST_COMPONENT_H_
