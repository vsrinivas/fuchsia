// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/mock_system_interface.h"

namespace debug_agent {

std::unique_ptr<ProcessHandle> MockSystemInterface::GetProcess(zx_koid_t process_koid) const {
  return root_job_.FindProcess(process_koid);
}

}  // namespace debug_agent
