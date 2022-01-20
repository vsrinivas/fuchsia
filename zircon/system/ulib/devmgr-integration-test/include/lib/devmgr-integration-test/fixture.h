// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DEVMGR_INTEGRATION_TEST_FIXTURE_H_
#define LIB_DEVMGR_INTEGRATION_TEST_FIXTURE_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.process.lifecycle/cpp/wire.h>
#include <lib/async/dispatcher.h>
#include <lib/devmgr-launcher/launch.h>
#include <lib/fit/function.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/zx/job.h>
#include <lib/zx/time.h>
#include <zircon/syscalls/exception.h>

#include <fbl/string.h>
#include <fbl/unique_fd.h>

namespace devmgr_integration_test {

class IsolatedDevmgr {
 public:
  IsolatedDevmgr();
  ~IsolatedDevmgr();

  IsolatedDevmgr(const IsolatedDevmgr&) = delete;
  IsolatedDevmgr& operator=(const IsolatedDevmgr&) = delete;

  IsolatedDevmgr(IsolatedDevmgr&& other);
  IsolatedDevmgr& operator=(IsolatedDevmgr&& other);

  // Get an args structure pre-populated with the test sysdev driver, the
  // test control driver, and the test driver directory.
  static devmgr_launcher::Args DefaultArgs();

  // Launch a new isolated devmgr.  The instance will be destroyed when
  // |*out|'s dtor runs.
  // |dispatcher| let's you choose which async loop the exception handler runs on.
  static zx_status_t Create(devmgr_launcher::Args args, IsolatedDevmgr* out);
  static zx_status_t Create(devmgr_launcher::Args args, async_dispatcher_t* dispatcher,
                            IsolatedDevmgr* out);

  // Get a fd to the root of the isolate devmgr's devfs.  This fd
  // may be used with openat() and fdio_watch_directory().
  const fbl::unique_fd& devfs_root() const { return devfs_root_; }
  fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root_dir() const { return svc_root_dir_; }
  fidl::UnownedClientEnd<fuchsia_io::Directory> fshost_outgoing_dir() const {
    return fshost_outgoing_dir_;
  }
  fidl::UnownedClientEnd<fuchsia_process_lifecycle::Lifecycle> component_lifecycle_svc() const {
    return component_lifecycle_client_;
  }

  fidl::ClientEnd<fuchsia_io::Directory> TakeSvcRootDir() { return std::move(svc_root_dir_); }

  // Expose devfs in component outgoing directory.
  zx_status_t AddDevfsToOutgoingDir(vfs::PseudoDir* outgoing_root_dir);

  // Notifies if driver manager job has an exception.
  void SetExceptionCallback(fit::function<void(zx_exception_info_t)> exception_callback);

  // Returns true if any process in driver manager process crashes.
  bool crashed() const;

  // Borrow the handle to the job containing the isolated devmgr.  This may be
  // used for things like binding to an exception port.
  const zx::job& containing_job() const { return job_; }

  const zx::process& driver_manager_process() const { return process_; }

  void reset() { *this = IsolatedDevmgr(); }

 private:
  using GetBootItemFunction = devmgr_launcher::GetBootItemFunction;

  // Path to the test sysdev driver
  static inline constexpr char kSysdevDriver[] = "/boot/driver/sysdev.so";

  // Opaque structure for the internal state used for serving /svc
  struct SvcLoopState;
  zx_status_t SetupSvcLoop(fidl::ServerEnd<fuchsia_io::Directory> bootsvc_server,
                           fidl::ClientEnd<fuchsia_io::Directory> fshost_outgoing_client,
                           fidl::ClientEnd<fuchsia_io::Directory> driver_index_outgoing_client,
                           GetBootItemFunction get_boot_item,
                           std::map<std::string, std::string>&& boot_args);

  struct ExceptionLoopState;
  zx_status_t SetupExceptionLoop(async_dispatcher_t* dispatcher, zx::channel exception_channel);

  // If |job_| exists, terminate it.
  void Terminate();

  // Job that contains the devmgr environment
  zx::job job_;

  // Process for driver manager.
  zx::process process_;

  // Channel for the root of outgoing services
  fidl::ClientEnd<fuchsia_io::Directory> svc_root_dir_;

  // Channel for the root of fshost
  fidl::ClientEnd<fuchsia_io::Directory> fshost_outgoing_dir_;

  // FD to the root of devmgr's devfs
  fbl::unique_fd devfs_root_;

  // Channel for component lifecycle events
  fidl::ClientEnd<fuchsia_process_lifecycle::Lifecycle> component_lifecycle_client_;

  // Opaque state associated with the async_loop_
  std::unique_ptr<SvcLoopState> svc_loop_state_;

  // Opaque state associated with the async_loop_
  std::unique_ptr<ExceptionLoopState> exception_loop_state_;
};

}  // namespace devmgr_integration_test

#endif  // LIB_DEVMGR_INTEGRATION_TEST_FIXTURE_H_
