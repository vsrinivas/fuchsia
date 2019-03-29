// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_SYSTEM_OBJECTS_DIRECTORY_H_
#define GARNET_BIN_APPMGR_SYSTEM_OBJECTS_DIRECTORY_H_

#include <fbl/string.h>
#include <fs/lazy-dir.h>
#include <lib/component/cpp/exposed_object.h>
#include <src/lib/fxl/strings/string_view.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>

namespace component {

class SystemObjectsDirectory : public component::ExposedObject {
 public:
  explicit SystemObjectsDirectory(zx::process process);

 private:
  zx_status_t GetProcessHandleStats(
      zx_info_process_handle_stats_t* process_handle_stats);

  class ThreadsDirectory : public component::ExposedObject {
   public:
    ThreadsDirectory(const zx::process* process);

   private:
    static constexpr size_t kMaxThreads = 2048;
    static constexpr uint64_t kAllId = 1;
    struct ThreadInfo {
      zx_koid_t koid;
      fbl::String name;
      zx::thread thread;
    };

    // Retrieves a list of ThreadInfos, one for each thread of the process.
    void GetThreads(fbl::Vector<ThreadInfo>* out);

    // Given a thread's handle, returns stats about the thread.
    zx_status_t GetThreadStats(zx_handle_t thread,
                               zx_info_thread_stats_t* thread_stats);

    const zx::process* process_;
  };

  class MemoryDirectory : public component::ExposedObject {
   public:
    MemoryDirectory(const zx::process* process);

   private:
    zx_status_t GetTaskStats(zx_info_task_stats_t* task_stats);

    const zx::process* process_;
  };

  zx::process process_;
  std::unique_ptr<ThreadsDirectory> threads_;
  std::unique_ptr<MemoryDirectory> memory_;
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_SYSTEM_OBJECTS_DIRECTORY_H_
