// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/mock_system_interface.h"

namespace debug_agent {

std::unique_ptr<JobHandle> MockSystemInterface::GetRootJob() const {
  return std::make_unique<MockJobHandle>(root_job_);
}

std::unique_ptr<JobHandle> MockSystemInterface::GetComponentRootJob() const { return nullptr; }

ComponentManager& MockSystemInterface::GetComponentManager() { return component_manager_; }

std::unique_ptr<BinaryLauncher> MockSystemInterface::GetLauncher() const {
  // Unimplemented in this mock.
  FX_NOTREACHED();
  return nullptr;
}

}  // namespace debug_agent
