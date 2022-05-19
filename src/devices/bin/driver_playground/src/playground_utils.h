// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_PLAYGROUND_SRC_PLAYGROUND_UTILS_H_
#define SRC_DEVICES_BIN_DRIVER_PLAYGROUND_SRC_PLAYGROUND_UTILS_H_

#include <lib/fidl/llcpp/string_view.h>

#include <vector>

namespace playground_utils {

std::vector<std::string> ExtractStringArgs(std::string_view tool_name,
                                           fidl::VectorView<fidl::StringView> args);

std::vector<const char*> ConvertToArgv(const std::vector<std::string>& str_argv);

std::string GetNameForResolve(std::string_view default_package_url, std::string_view tool_name);

}  // namespace playground_utils

#endif  // SRC_DEVICES_BIN_DRIVER_PLAYGROUND_SRC_PLAYGROUND_UTILS_H_
