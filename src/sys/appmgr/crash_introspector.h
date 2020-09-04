// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_CRASH_INTROSPECTOR_H_
#define SRC_SYS_APPMGR_CRASH_INTROSPECTOR_H_

#include <fuchsia/sys/internal/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <zircon/types.h>

#include <memory>

#include "lib/async/cpp/task.h"
#include "lib/async/cpp/wait.h"
#include "lib/fitx/internal/result.h"
#include "lib/zx/channel.h"
#include "lib/zx/thread.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace component {

// Implements |CrashIntrospect| fidl service and keeps a cache of all crashed threads in monitored
// jobs. The cached results are automatically deleted after some time or when retrieved using
// |FindComponentByThreadKoid| call.
class CrashIntrospector : public fuchsia::sys::internal::CrashIntrospect {
 public:
  CrashIntrospector();
  virtual ~CrashIntrospector() override;

  // Register the job to be monitored for thread crashes and associate it with |component_info|.
  void RegisterJob(const zx::job& job, fuchsia::sys::internal::SourceIdentity component_info);

  // Removes and returns the component associated with crashed thread which is cached in this
  // class.
  void FindComponentByThreadKoid(zx_koid_t thread_koid,
                                 FindComponentByThreadKoidCallback callback) override;

  void AddBinding(fidl::InterfaceRequest<fuchsia::sys::internal::CrashIntrospect> request);

 private:
  // Class to monitor an individual job.
  class CrashMonitor {
   public:
    CrashMonitor(fxl::WeakPtr<CrashIntrospector> introspector, zx::channel exception_channel,
                 fuchsia::sys::internal::SourceIdentity component_info);
    ~CrashMonitor();

   private:
    void CrashHandler(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                      const zx_packet_signal* signal);
    fxl::WeakPtr<CrashIntrospector> introspector_;
    fuchsia::sys::internal::SourceIdentity component_info_;
    zx::channel exception_channel_;
    async::WaitMethod<CrashMonitor, &CrashMonitor::CrashHandler> wait_;

    FXL_DISALLOW_COPY_AND_ASSIGN(CrashMonitor);
  };

  // Adds thread and associated |component_info| to the cache.
  void AddThreadToCache(const zx::thread& thread,
                        const fuchsia::sys::internal::SourceIdentity& component_info);

  // Removes thread from the cache and returns |component_info| if available.
  // Returns false if thread is not in the cache.
  fitx::result<bool, fuchsia::sys::internal::SourceIdentity> RemoveThreadFromCache(
      zx_koid_t thread_koid);

  std::unique_ptr<CrashMonitor> ExtractMonitor(const CrashMonitor* monitor);

  fxl::WeakPtrFactory<CrashIntrospector> weak_ptr_factory_;
  // Keep monitors for safe keeping till they are running.
  std::map<const CrashMonitor*, std::unique_ptr<CrashMonitor>> monitors_;

  // Stores associated |component_info| and a task which will auto delete it from the cache in a
  // fixed time (as defined in implementation file).
  std::map<zx_koid_t,
           std::pair<std::unique_ptr<async::TaskClosure>, fuchsia::sys::internal::SourceIdentity>>
      thread_cache_;

  fidl::BindingSet<fuchsia::sys::internal::CrashIntrospect> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CrashIntrospector);
};

}  // namespace component

#endif  // SRC_SYS_APPMGR_CRASH_INTROSPECTOR_H_
