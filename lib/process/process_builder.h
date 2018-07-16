// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_PROCESS_PROCESS_BUILDER_H_
#define GARNET_LIB_PROCESS_PROCESS_BUILDER_H_

#include <fuchsia/process/cpp/fidl.h>
#include <lib/zx/vmo.h>

#include <string>
#include <vector>

namespace process {

// Creates a process step-by-step.
//
// For most use cases, |fdio_spawn| and |fdio_spawn_etc| are better choices for
// creating processes. However, if you need to manipulate the process after it
// has been created but before it has been started, you might want to use this
// class.
class ProcessBuilder {
 public:
  // Creates a process builder that uses the |fuchsia.process.Launcher| service
  // from the environment.
  //
  // The process is created in zx_job_default().
  ProcessBuilder();

  // Creates a process builder that will build a process in the given |job|.
  explicit ProcessBuilder(zx::job job);
  ~ProcessBuilder();

  ProcessBuilder(const ProcessBuilder&) = delete;
  ProcessBuilder& operator=(const ProcessBuilder&) = delete;

  // Use |executable| as the executable for the process.
  void LoadVMO(zx::vmo executable);

  // Load the executable for the process from the given |path|.
  zx_status_t LoadPath(const std::string& path);

  // Append arguments to the argument list for the process.
  //
  // Safe to call mutliple times.
  void AddArgs(const std::vector<std::string>& argv);

  // Adds the given handle to the handle list for the process.
  //
  // Safe to call mutliple times.
  void AddHandle(uint32_t id, zx::handle handle);

  // Adds the given handles to the handle list for the process.
  //
  // Safe to call mutliple times.
  void AddHandles(std::vector<fuchsia::process::HandleInfo> handles);

  // Provide |job| to the process as PA_JOB_DEFAULT.
  //
  // By default, the created process will use this job when creating more
  // processes.
  //
  // Does not affect in which job the process is created.
  void SetDefaultJob(zx::job job);

  // Set a name for the process.
  //
  // The name purely descriptive and used in process listing and other
  // diagnostic tools. The name doesn't need to correspond to the path.
  //
  // If |AddArgs| is called with a non-empty vector, the name for the process
  // will be taken from |argv[0]| the first time |AddArgs| is called. You can
  // call this function either before or after |AddArgs| to override the name.
  //
  // If you never call |AddArgs| with a non-empty vector, you will need to call
  // this function to set a name for the process.
  void SetName(std::string name);

  // Passes the job in which the process will be created as the |PA_JOB_DEFAULT|
  // for the created process.
  //
  // Defaults to zx_job_default() unless you passed a job explicitly when
  // constructing this object.
  void CloneJob();

  // Clone the ldsvc for this process as the |PA_LOADER_SVC| for the created
  // process.
  //
  // The created process will use this service to load shared libraries. A
  // loader service of some kind is required in order to create the process
  // to load the ELF |INTERP|.
  void CloneLdsvc();

  // Clone the FDIO namespace for this process as the namespace for the created
  // process.
  void CloneNamespace();

  // Clone the STDIO for this process as the namespace for the created process.
  //
  // If any of stdin, stdout, or stderr are closed (or otherwise not clonable),
  // they are ignored.
  void CloneStdio();

  // Clone the environ for this process as the environ for the created process.
  void CloneEnvironment();

  // Calls |CloneJob|, |CloneLdsvc|, |CloneNamespace|, |CloneStdio|, and
  // |CloneEnvironment|.
  void CloneAll();

  // Clone the local file descriptor |local_fd| as the file descriptor
  // |target_fd| in the created process.
  zx_status_t CloneFileDescriptor(int local_fd, int target_fd);

  // Create the process.
  //
  // Upon success, the process is created but not started. At this point, the
  // process can be inspected and manipulated using |data|.
  //
  // |error_message| is optional.
  zx_status_t Prepare(std::string* error_message);

  // Actually start the process.
  //
  // Only valid after |Prepare| has been called successfully.
  zx_status_t Start(zx::process* process_out);

  // Information about the process prior to start.
  //
  // Valid only between |Prepare| and |Start|.
  const fuchsia::process::ProcessStartData& data() const { return data_; }

 private:
  fuchsia::process::LauncherSyncPtr launcher_;
  fuchsia::process::LaunchInfo launch_info_;
  fuchsia::process::ProcessStartData data_;
  fidl::VectorPtr<fuchsia::process::HandleInfo> handles_;
};

}  // namespace process

#endif  // GARNET_LIB_PROCESS_PROCESS_BUILDER_H_
