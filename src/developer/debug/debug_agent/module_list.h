// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MODULE_LIST_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MODULE_LIST_H_

#include <vector>

#include "src/developer/debug/ipc/records.h"

namespace debug_agent {

class ProcessHandle;

// Maintains a sorted list of modules loaded in a process.
class ModuleList {
 public:
  using ModuleVector = std::vector<debug_ipc::Module>;

  // Returns true if there were any changes, false if there were none.
  bool Update(const ProcessHandle& process, uint64_t dl_debug_addr);

  // This vector will always be sorted by load address.
  const ModuleVector& modules() const { return modules_; }

 private:
  ModuleVector modules_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MODULE_LIST_H_
