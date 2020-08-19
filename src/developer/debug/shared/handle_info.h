// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_HANDLE_INFO_H_
#define SRC_DEVELOPER_DEBUG_SHARED_HANDLE_INFO_H_

#include <stdint.h>

#include <string>
#include <vector>

namespace debug_ipc {

// Converts a uint32_t handle type to a string. Returns "<unknown>" on failure.
std::string HandleTypeToString(uint32_t handle_type);

// Returns a vector of strings, one for each right set.
std::vector<std::string> HandleRightsToStrings(uint32_t handle_rights);

// Returns a vector of right strings separated by "|" characters.
std::string HandleRightsToString(uint32_t handle_rights);

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_SHARED_HANDLE_INFO_H_
