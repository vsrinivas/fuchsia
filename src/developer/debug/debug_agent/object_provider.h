// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_OBJECT_PROVIDER_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_OBJECT_PROVIDER_H_

#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <zircon/types.h>

#include <string>
#include <vector>

namespace debug_agent {

// Main interface for getting object data from the kernel. Think handles and koids.
// Tests should inherit from this interface in order to mock the system.
class ObjectProvider {
 public:
  ObjectProvider();
  virtual ~ObjectProvider();

  virtual zx::thread ThreadForKoid(zx_handle_t process, zx_koid_t thread_koid) const;

  virtual zx_koid_t KoidForObject(zx_handle_t object) const;
  inline zx_koid_t KoidForObject(const zx::object_base& object) const {
    return KoidForObject(object.get());
  }

  // Returns the empty string on failure. The empty string might also be a valid
  // name, so this is intended for cases where failure isn't critical to detect.
  virtual std::string NameForObject(zx_handle_t object) const;
  inline std::string NameForObject(const zx::object_base& object) const {
    return NameForObject(object.get());
  }

  // Returns a process handle for the given process koid.
  // The process will be not is_valid() on failure.
  virtual zx::process GetProcessFromKoid(zx_koid_t koid) const;

  // Returns a job handle for the given job koid. The job will be not is_valid() on failure.
  virtual zx::job GetJobFromKoid(zx_koid_t koid) const;

  // Returns the KOID associated with the given job. Returns 0 on failure.
  virtual zx::job GetRootJob() const;
  virtual zx_koid_t GetRootJobKoid() const;
  virtual zx_koid_t GetComponentJobKoid() const;

  // Returns the koids of the child objects of the given parent object. The child kind is passed to
  // zx_object_get_info. It is typically ZX_INFO_PROCESS_THREADS, ZX_INFO_JOB_CHILDREN, or
  // ZX_INFO_JOB_PROCESSES.
  virtual std::vector<zx_koid_t> GetChildKoids(zx_handle_t parent, uint32_t child_kind) const;

  virtual zx_status_t GetChild(zx_handle_t parent, zx_koid_t koid, uint32_t rights,
                               zx_handle_t* child) const;

  // Returns the specified kind of child objects.
  virtual std::vector<zx::job> GetChildJobs(zx_handle_t job) const;
  virtual std::vector<zx::process> GetChildProcesses(zx_handle_t job) const;
  virtual std::vector<zx::thread> GetChildThreads(zx_handle_t process) const;

  virtual zx::process GetProcessFromException(zx_handle_t exception_token) const;
  virtual zx::thread GetThreadFromException(zx_handle_t exception_token) const;

  virtual zx_status_t ListHandleRights(zx_handle_t handle, std::string* out) const;

  template <typename T>
  zx_status_t ListHandleRights(const zx::object<T>& object, std::string* out) const {
    return ListHandleRights(object.get(), out);
  }

  virtual zx_status_t Kill(zx_handle_t);
  template <typename T>
  zx_status_t Kill(const zx::object<T>& object) {
    return Kill(object.get());
  }
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_OBJECT_PROVIDER_H_
