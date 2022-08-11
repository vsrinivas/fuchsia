// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_SYSTEM_INTERFACE_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_SYSTEM_INTERFACE_H_

#include <map>
#include <memory>
#include <vector>

#include "src/developer/debug/debug_agent/job_handle.h"
#include "src/developer/debug/debug_agent/process_handle.h"
#include "src/developer/debug/ipc/records.h"

namespace debug_agent {

class BinaryLauncher;
class ComponentManager;
class LimboProvider;

// Abstract interface that represents the system. This is eqivalent to ProcessHandle for processes
// but for the system (for which there's not a clearly owned handle).
class SystemInterface {
 public:
  SystemInterface() = default;
  virtual ~SystemInterface() = default;

  // SystemInterface shouldn't be movable because ComponentManager keeps a pointer to us.
  SystemInterface(SystemInterface&&) = delete;

  // Returns system statistics.
  virtual uint32_t GetNumCpus() const = 0;
  virtual uint64_t GetPhysicalMemory() const = 0;

  // Returns a pointer to a job owned by this object (the root job is queried frequently). Returns
  // null if the root job was not available.
  virtual std::unique_ptr<JobHandle> GetRootJob() const = 0;

  // Creates a BinaryLauncher. This is a creator for a launcher instead of
  //   std::unique_ptr<ProcessHandle> LaunchProcess(...);
  // because the launch on Fuchsia requires two steps with possibly some caller-specific logic in
  // between.
  //
  // If this requires mocking in the future, we should probably make the BinaryLauncher an abstract
  // interface that can itself be mocked.
  virtual std::unique_ptr<BinaryLauncher> GetLauncher() const = 0;

  // Returns the component manager for this system. Ownership stays with the SystemInterface.
  virtual ComponentManager& GetComponentManager() = 0;

  // Returns a reference to the limbo provider. This gives access to processes that have crashed and
  // are being held for attaching to the debugger. The limbo provider may have failed, in which
  // case it will be !Valid(). The reference is owned by this class.
  virtual LimboProvider& GetLimboProvider() = 0;

  // Returns a string representation of the current system version.
  virtual std::string GetSystemVersion() = 0;

  // Non-virtual helpers ---------------------------------------------------------------------------
  //
  // These all use the virtual interface above to implement their functionality.

  // Collects the process tree starting from the given job handle.
  debug_ipc::ProcessTreeRecord GetProcessTree();

  // Returns a handle to the job/process with the given koid. Returns an empty pointer if it was not
  // found. This can also happen if the debug_agent doesn't have permission to see it.
  std::unique_ptr<JobHandle> GetJob(zx_koid_t job_koid) const;
  std::unique_ptr<ProcessHandle> GetProcess(zx_koid_t process_koid) const;

  // Returns the koid of the parent job given the koid of a job, or ZX_KOID_INVALID for root job.
  zx_koid_t GetParentJobKoid(zx_koid_t job);

 private:
  // Refresh the parent_jobs_ mapping.
  void RefreshParentJobs();

  // Cached mapping from child job koid to parent job koid.
  std::map<zx_koid_t, zx_koid_t> parent_jobs_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_SYSTEM_INTERFACE_H_
