// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/object_provider.h"

#include <fcntl.h>
#include <fuchsia/boot/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <zircon/syscalls/object.h>

#include "src/developer/debug/shared/zx_status.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/logging.h"

namespace debug_agent {

namespace {

template <typename ResultObject>
std::vector<ResultObject> GetChildObjects(const ObjectProvider& provider, zx_handle_t parent,
                                          uint32_t child_kind) {
  auto koids = provider.GetChildKoids(parent, child_kind);

  std::vector<ResultObject> result;
  result.reserve(koids.size());

  for (zx_koid_t koid : koids) {
    zx_handle_t handle;
    if (zx_object_get_child(parent, koid, ZX_RIGHT_SAME_RIGHTS, &handle) == ZX_OK)
      result.push_back(ResultObject(handle));
  }
  return result;
}

// Searches the process tree rooted at "job" for a process with the given
// koid. If found, puts it in *out* and returns true.
bool FindProcess(const ObjectProvider& provider, const zx::job& job, zx_koid_t search_for,
                 zx::process* out) {
  for (auto& proc : provider.GetChildProcesses(job.get())) {
    if (provider.KoidForObject(proc) == search_for) {
      *out = std::move(proc);
      return true;
    }
  }

  for (const auto& job : provider.GetChildJobs(job.get())) {
    if (FindProcess(provider, job, search_for, out))
      return true;
  }
  return false;
}

// Searches root job for a job with the given
// koid. If found, puts it in *out* and returns true.
bool FindJob(const ObjectProvider& provider, zx::job root_job, zx_koid_t search_for, zx::job* out) {
  if (provider.KoidForObject(root_job) == search_for) {
    out->reset(root_job.release());
    return true;
  }

  auto child_jobs = provider.GetChildJobs(root_job.get());
  for (auto& child_job : child_jobs) {
    if (FindJob(provider, zx::job(child_job.release()), search_for, out))
      return true;
  }
  return false;
}

}  // namespace

ObjectProvider::ObjectProvider() = default;
ObjectProvider::~ObjectProvider() = default;

zx::thread ObjectProvider::ThreadForKoid(zx_handle_t process, zx_koid_t thread_koid) const {
  zx_handle_t thread_handle = ZX_HANDLE_INVALID;
  if (zx_object_get_child(process, thread_koid, ZX_RIGHT_SAME_RIGHTS, &thread_handle) != ZX_OK)
    return zx::thread();
  return zx::thread(thread_handle);
}

zx_koid_t ObjectProvider::KoidForObject(zx_handle_t object) const {
  zx_info_handle_basic_t info;
  if (zx_object_get_info(object, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr) !=
      ZX_OK)
    return 0;
  return info.koid;
}

std::string ObjectProvider::NameForObject(zx_handle_t object) const {
  char name[ZX_MAX_NAME_LEN];
  if (zx_object_get_property(object, ZX_PROP_NAME, name, sizeof(name)) == ZX_OK)
    return std::string(name);
  return std::string();
}

std::vector<zx_koid_t> ObjectProvider::GetChildKoids(zx_handle_t parent,
                                                     uint32_t child_kind) const {
  constexpr size_t kNumExtraKoids = 10u;

  size_t actual = 0;
  size_t available = 0;
  std::vector<zx_koid_t> result;

  // This is inherently racy, but we retry once with a bit of slop to try to
  // get a complete list.
  for (int pass = 0; pass < 2; pass++) {
    if (actual < available)
      result.resize(available + kNumExtraKoids);
    zx_status_t status = zx_object_get_info(parent, child_kind, result.data(),
                                            result.size() * sizeof(zx_koid_t), &actual, &available);
    if (status != ZX_OK || actual == available)
      break;
  }
  result.resize(actual);
  return result;
}

zx_status_t ObjectProvider::GetChild(zx_handle_t parent, zx_koid_t koid, uint32_t rights,
                                     zx_handle_t* child) const {
  return zx_object_get_child(parent, koid, ZX_RIGHT_SAME_RIGHTS, child);
}

std::vector<zx::job> ObjectProvider::GetChildJobs(zx_handle_t job) const {
  return GetChildObjects<zx::job>(*this, job, ZX_INFO_JOB_CHILDREN);
}

std::vector<zx::process> ObjectProvider::GetChildProcesses(zx_handle_t job) const {
  return GetChildObjects<zx::process>(*this, job, ZX_INFO_JOB_PROCESSES);
}

std::vector<zx::thread> ObjectProvider::GetChildThreads(zx_handle_t process) const {
  return GetChildObjects<zx::thread>(*this, process, ZX_INFO_PROCESS_THREADS);
}

zx::process ObjectProvider::GetProcessFromException(zx_handle_t exception) const {
  zx_handle_t process_handle = ZX_HANDLE_INVALID;
  zx_status_t status = zx_exception_get_process(exception, &process_handle);
  FXL_DCHECK(status == ZX_OK) << "Got: " << debug_ipc::ZxStatusToString(status);

  return zx::process(process_handle);
}

zx::thread ObjectProvider::GetThreadFromException(zx_handle_t exception) const {
  zx_handle_t thread_handle = ZX_HANDLE_INVALID;
  zx_status_t status = zx_exception_get_thread(exception, &thread_handle);
  FXL_DCHECK(status == ZX_OK) << "Got: " << debug_ipc::ZxStatusToString(status);

  return zx::thread(thread_handle);
}

zx::process ObjectProvider::GetProcessFromKoid(zx_koid_t koid) const {
  zx::process result;
  FindProcess(*this, GetRootJob(), koid, &result);
  return result;
}

zx::job ObjectProvider::GetJobFromKoid(zx_koid_t koid) const {
  zx::job result;
  FindJob(*this, GetRootJob(), koid, &result);
  return result;
}

zx_koid_t ObjectProvider::GetRootJobKoid() const { return KoidForObject(GetRootJob()); }

// The hub writes the job it uses to create components in a special file.
//
// This is note quite correct. This code actually returns the job that contains
// the debug agent itself, which is usually the right thing because the debug
// agent normally runs in the component root.
//
// TODO: Find the correct job even when the debug agent is run from elsewhere.
zx_koid_t ObjectProvider::GetComponentJobKoid() const {
  std::string koid_str;
  bool file_read = files::ReadFileToString("/hub/job-id", &koid_str);
  if (!file_read) {
    FXL_LOG(ERROR) << "Not able to read job-id: " << strerror(errno);
    return 0;
  }

  char* end = NULL;
  uint64_t koid = strtoul(koid_str.c_str(), &end, 10);
  if (*end) {
    FXL_LOG(ERROR) << "Invalid job-id: " << koid_str.c_str();
    return 0;
  }

  return koid;
}

// TODO(brettw) this is based on the code in Zircon's task-utils which uses
// this hack to get the root job handle. It will likely need to be updated
// when a better way to get the root job is found.
zx::job ObjectProvider::GetRootJob() const {
  int fd = open("/svc/fuchsia.boot.RootJob", O_RDWR);
  if (fd < 0) {
    FXL_NOTREACHED();
    return zx::job();
  }

  zx::channel channel;
  zx_status_t status = fdio_get_service_handle(fd, channel.reset_and_get_address());
  if (status != ZX_OK) {
    FXL_NOTREACHED();
    return zx::job();
  }

  zx_handle_t root_job;
  zx_status_t fidl_status = fuchsia_boot_RootJobGet(channel.get(), &root_job);
  if (fidl_status != ZX_OK) {
    FXL_NOTREACHED();
    return zx::job();
  }
  return zx::job(root_job);
}

zx_status_t ObjectProvider::ListHandleRights(zx_handle_t handle, std::string* out) const {
  zx_info_handle_basic info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK)
    return status;

  // clang-format off
  std::stringstream ss;
  if (info.rights & ZX_RIGHT_DUPLICATE) { ss << "ZX_RIGHT_DUPLICATE" << std::endl; }
  if (info.rights & ZX_RIGHT_TRANSFER) { ss << "ZX_RIGHT_TRANSFER" << std::endl; }
  if (info.rights & ZX_RIGHT_READ) { ss << "ZX_RIGHT_READ" << std::endl; }
  if (info.rights & ZX_RIGHT_WRITE) { ss << "ZX_RIGHT_WRITE" << std::endl; }
  if (info.rights & ZX_RIGHT_EXECUTE) { ss << "ZX_RIGHT_EXECUTE" << std::endl; }
  if (info.rights & ZX_RIGHT_MAP) { ss << "ZX_RIGHT_MAP" << std::endl; }
  if (info.rights & ZX_RIGHT_GET_PROPERTY) { ss << "ZX_RIGHT_GET_PROPERTY" << std::endl; }
  if (info.rights & ZX_RIGHT_SET_PROPERTY) { ss << "ZX_RIGHT_SET_PROPERTY" << std::endl; }
  if (info.rights & ZX_RIGHT_ENUMERATE) { ss << "ZX_RIGHT_ENUMERATE" << std::endl; }
  if (info.rights & ZX_RIGHT_DESTROY) { ss << "ZX_RIGHT_DESTROY" << std::endl; }
  if (info.rights & ZX_RIGHT_SET_POLICY) { ss << "ZX_RIGHT_SET_POLICY" << std::endl; }
  if (info.rights & ZX_RIGHT_GET_POLICY) { ss << "ZX_RIGHT_GET_POLICY" << std::endl; }
  if (info.rights & ZX_RIGHT_SIGNAL) { ss << "ZX_RIGHT_SIGNAL" << std::endl; }
  if (info.rights & ZX_RIGHT_SIGNAL_PEER) { ss << "ZX_RIGHT_SIGNAL_PEER" << std::endl; }
  if (info.rights & ZX_RIGHT_WAIT) { ss << "ZX_RIGHT_WAIT" << std::endl; }
  if (info.rights & ZX_RIGHT_INSPECT) { ss << "ZX_RIGHT_INSPECT" << std::endl; }
  if (info.rights & ZX_RIGHT_MANAGE_JOB) { ss << "ZX_RIGHT_MANAGE_JOB" << std::endl; }
  if (info.rights & ZX_RIGHT_MANAGE_PROCESS) { ss << "ZX_RIGHT_MANAGE_PROCESS" << std::endl; }
  if (info.rights & ZX_RIGHT_MANAGE_THREAD) { ss << "ZX_RIGHT_MANAGE_THREAD" << std::endl; }
  if (info.rights & ZX_RIGHT_APPLY_PROFILE) { ss << "ZX_RIGHT_APPLY_PROFILE" << std::endl; }
  if (info.rights & ZX_RIGHT_SAME_RIGHTS) { ss << "ZX_RIGHT_SAME_RIGHTS" << std::endl; }
  // clang-format on

  *out = ss.str();
  return ZX_OK;
}

zx_status_t ObjectProvider::Kill(zx_handle_t handle) { return zx_task_kill(handle); }

}  // namespace debug_agent
