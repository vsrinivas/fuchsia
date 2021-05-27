// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/zircon_system_interface.h"

#include "src/developer/debug/debug_agent/zircon_binary_launcher.h"
#include "src/developer/debug/debug_agent/zircon_job_handle.h"
#include "src/developer/debug/debug_agent/zircon_process_handle.h"
#include "src/developer/debug/debug_agent/zircon_utils.h"

namespace debug_agent {

ZirconSystemInterface::ZirconSystemInterface()
    : root_job_(zircon::GetRootJob()),
      services_(sys::ServiceDirectory::CreateFromNamespace()),
      component_manager_(services_),
      limbo_provider_(services_) {}

uint32_t ZirconSystemInterface::GetNumCpus() const { return zx_system_get_num_cpus(); }

uint64_t ZirconSystemInterface::GetPhysicalMemory() const { return zx_system_get_physmem(); }

std::unique_ptr<JobHandle> ZirconSystemInterface::GetRootJob() const {
  return std::make_unique<ZirconJobHandle>(root_job_);
}

std::unique_ptr<JobHandle> ZirconSystemInterface::GetComponentRootJob() const {
  if (!root_job_.GetNativeHandle().is_valid())
    return nullptr;

  zx_koid_t component_root_koid = zircon::GetComponentRootJobKoid();
  if (component_root_koid == ZX_KOID_INVALID)
    return nullptr;

  return root_job_.FindJob(component_root_koid);
}

std::unique_ptr<BinaryLauncher> ZirconSystemInterface::GetLauncher() const {
  return std::make_unique<ZirconBinaryLauncher>(services_);
}

ComponentManager& ZirconSystemInterface::GetComponentManager() { return component_manager_; }

std::string ZirconSystemInterface::GetSystemVersion() { return zx_system_get_version_string(); }

}  // namespace debug_agent
