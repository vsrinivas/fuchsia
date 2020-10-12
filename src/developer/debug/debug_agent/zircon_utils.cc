// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/zircon_utils.h"

#include <fuchsia/kernel/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/files/file.h"

namespace debug_agent {
namespace zircon {

namespace {

template <typename ResultObject>
std::vector<ResultObject> GetChildObjects(const zx::object_base& parent, uint32_t child_kind) {
  auto koids = GetChildKoids(parent, child_kind);

  std::vector<ResultObject> result;
  result.reserve(koids.size());

  for (zx_koid_t koid : koids) {
    zx_handle_t handle;
    if (zx_object_get_child(parent.get(), koid, ZX_RIGHT_SAME_RIGHTS, &handle) == ZX_OK)
      result.push_back(ResultObject(handle));
  }
  return result;
}

}  // namespace

zx_koid_t KoidForObject(const zx::object_base& object) {
  zx_info_handle_basic_t info;
  if (object.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr) != ZX_OK)
    return ZX_KOID_INVALID;
  return info.koid;
}

std::string NameForObject(const zx::object_base& object) {
  char name[ZX_MAX_NAME_LEN];
  if (object.get_property(ZX_PROP_NAME, name, sizeof(name)) == ZX_OK)
    return std::string(name);
  return std::string();
}

zx::job GetRootJob() {
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

std::vector<zx_koid_t> GetChildKoids(const zx::object_base& parent, uint32_t child_kind) {
  constexpr size_t kNumExtraKoids = 10u;

  size_t actual = 0;
  size_t available = 0;
  std::vector<zx_koid_t> result;

  // This is inherently racy, but we retry once with a bit of slop to try to get a complete list.
  for (int pass = 0; pass < 2; pass++) {
    if (actual < available)
      result.resize(available + kNumExtraKoids);
    zx_status_t status = parent.get_info(child_kind, result.data(),
                                         result.size() * sizeof(zx_koid_t), &actual, &available);
    if (status != ZX_OK || actual == available)
      break;
  }
  result.resize(actual);
  return result;
}

std::vector<zx::thread> GetChildThreads(const zx::process& process) {
  return GetChildObjects<zx::thread>(process, ZX_INFO_PROCESS_THREADS);
}

std::vector<zx::process> GetChildProcesses(const zx::job& job) {
  return GetChildObjects<zx::process>(job, ZX_INFO_JOB_PROCESSES);
}

std::vector<zx::job> GetChildJobs(const zx::job& job) {
  return GetChildObjects<zx::job>(job, ZX_INFO_JOB_CHILDREN);
}

}  // namespace zircon
}  // namespace debug_agent
