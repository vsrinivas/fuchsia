// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_playground/src/playground_utils.h"

namespace playground_utils {

std::vector<std::string> ExtractStringArgs(std::string_view tool_name,
                                           fidl::VectorView<fidl::StringView> args) {
  std::vector<std::string> str_argv;
  str_argv.emplace_back(std::string(tool_name));
  for (fidl::StringView arg : args) {
    str_argv.emplace_back(std::string(arg.get()));
  }

  return str_argv;
}

std::vector<const char*> ConvertToArgv(const std::vector<std::string>& str_argv) {
  std::vector<const char*> argv;
  argv.reserve(str_argv.size() + 1);
  for (const std::string& arg : str_argv) {
    argv.push_back(arg.c_str());
  }

  argv.push_back(nullptr);
  return argv;
}

std::string GetNameForResolve(std::string_view default_package_url, std::string_view tool_name) {
  std::string name_for_resolve(tool_name);

  if (name_for_resolve.find("fuchsia-pkg://") != 0 &&
      name_for_resolve.find("fuchsia-boot://") != 0) {
    name_for_resolve = std::string(default_package_url) + std::string(tool_name);
  }

  return name_for_resolve;
}

}  // namespace playground_utils
