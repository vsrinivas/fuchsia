// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_IPC_FILTER_UTILS_H_
#define SRC_DEVELOPER_DEBUG_IPC_FILTER_UTILS_H_

#include <string>

#include "src/developer/debug/ipc/records.h"

namespace debug_ipc {

// Matches the filter with the given process_name and the component info, ignoring the job_koid.
bool FilterMatches(const Filter& filter, const std::string& process_name,
                   const std::optional<ComponentInfo>& component);

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_IPC_FILTER_UTILS_H_
