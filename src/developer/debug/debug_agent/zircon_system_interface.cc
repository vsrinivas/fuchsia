// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/zircon_system_interface.h"

#include <fuchsia/kernel/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>

#include "src/developer/debug/debug_agent/zircon_binary_launcher.h"
#include "src/developer/debug/debug_agent/zircon_job_handle.h"
#include "src/developer/debug/debug_agent/zircon_process_handle.h"
#include "src/developer/debug/debug_agent/zircon_utils.h"
#include "src/lib/files/file.h"

namespace debug_agent {

namespace {

// Returns an !is_valid() job object on failure.
zx::job GetRootZxJob() {
  zx::job root_job;
  fuchsia::kernel::RootJobSyncPtr root_job_ptr;

  std::string root_job_path("/svc/");
  root_job_path.append(fuchsia::kernel::RootJob::Name_);

  zx_status_t status = fdio_service_connect(root_job_path.c_str(),
                                            root_job_ptr.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    FX_NOTREACHED();
    return zx::job();
  }

  status = root_job_ptr->Get(&root_job);
  if (status != ZX_OK) {
    FX_NOTREACHED();
    return zx::job();
  }
  return root_job;
}

// The hub writes the job it uses to create components in a special file.
//
// This is note quite correct. This code actually returns the job that contains the debug agent
// itself, which is usually the right thing because the debug agent normally runs in the component
// root.
//
// TODO: Find the correct job even when the debug agent is run from elsewhere.
zx_koid_t GetComponentRootJobKoid() {
  // TODO(bug 56725) this function seems to no longer work and the job-id file is not found.
  std::string koid_str;
  bool file_read = files::ReadFileToString("/hub/job-id", &koid_str);
  if (!file_read)
    return ZX_KOID_INVALID;

  char* end = NULL;
  uint64_t koid = strtoul(koid_str.c_str(), &end, 10);
  if (*end)
    return ZX_KOID_INVALID;

  return koid;
}

}  // namespace

ZirconSystemInterface::ZirconSystemInterface()
    : services_(sys::ServiceDirectory::CreateFromNamespace()),
      component_manager_(services_),
      limbo_provider_(services_) {
  if (zx::job zx_root = GetRootZxJob(); zx_root.is_valid())
    root_job_ = std::make_unique<ZirconJobHandle>(std::move(zx_root));
}

uint32_t ZirconSystemInterface::GetNumCpus() const { return zx_system_get_num_cpus(); }

uint64_t ZirconSystemInterface::GetPhysicalMemory() const { return zx_system_get_physmem(); }

std::unique_ptr<JobHandle> ZirconSystemInterface::GetRootJob() const {
  if (root_job_)
    return std::make_unique<ZirconJobHandle>(*root_job_);
  return nullptr;
}

std::unique_ptr<JobHandle> ZirconSystemInterface::GetComponentRootJob() const {
  if (!root_job_)
    return nullptr;

  zx_koid_t component_root_koid = GetComponentRootJobKoid();
  if (component_root_koid == ZX_KOID_INVALID)
    return nullptr;

  return root_job_->FindJob(component_root_koid);
}

std::unique_ptr<BinaryLauncher> ZirconSystemInterface::GetLauncher() const {
  return std::make_unique<ZirconBinaryLauncher>(services_);
}

ComponentManager& ZirconSystemInterface::GetComponentManager() { return component_manager_; }

std::string ZirconSystemInterface::GetSystemVersion() { return zx_system_get_version_string(); }

}  // namespace debug_agent
