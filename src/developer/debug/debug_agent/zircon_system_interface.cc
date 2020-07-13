// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/zircon_system_interface.h"

#include "src/developer/debug/debug_agent/zircon_utils.h"

namespace debug_agent {

ZirconSystemInterface::ZirconSystemInterface() : root_job_(zircon::GetRootJob()) {}

std::unique_ptr<ProcessHandle> ZirconSystemInterface::GetProcess(zx_koid_t process_koid) const {
  return root_job_.FindProcess(process_koid);
}

}  // namespace debug_agent
