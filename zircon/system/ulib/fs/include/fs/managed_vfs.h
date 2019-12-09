// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_MANAGED_VFS_H_
#define FS_MANAGED_VFS_H_

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <lib/async/cpp/task.h>

#include <atomic>
#include <memory>

#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fs/internal/connection.h>
#include <fs/vfs.h>

namespace fs {

// A specialization of |Vfs| which provides a mechanism to tear down
// all active connections before it is destroyed.
//
// This class is thread-safe, but it is unsafe to shutdown the dispatch loop
// before shutting down the ManagedVfs object.
class ManagedVfs : public Vfs {
 public:
  ManagedVfs();
  explicit ManagedVfs(async_dispatcher_t* dispatcher);

  // The ManagedVfs destructor is only safe to execute if
  // no connections are actively registered.
  //
  // To ensure that this state is achieved, it is recommended that
  // clients issue a call to |Shutdown| before calling the destructor.
  ~ManagedVfs() override;

  // Asynchronously drop all connections managed by the VFS.
  //
  // Invokes |handler| once when all connections are destroyed.
  // It is safe to delete ManagedVfs from within the closure.
  //
  // It is unsafe to call Shutdown multiple times.
  void Shutdown(ShutdownCallback handler) override __TA_EXCLUDES(lock_);

 private:
  // Posts the task for OnShutdownComplete if it is safe to do so.
  void CheckForShutdownComplete() __TA_REQUIRES(lock_);

  // Identifies if the filesystem has fully terminated, and is
  // ready for "OnShutdownComplete" to execute.
  bool IsTerminated() const __TA_REQUIRES(lock_);

  // Invokes the handler from |Shutdown| once all connections have been
  // released. Additionally, unmounts all sub-mounted filesystems, if any
  // exist.
  void OnShutdownComplete(async_dispatcher_t*, async::TaskBase*, zx_status_t status)
      __TA_EXCLUDES(lock_);

  void RegisterConnection(std::unique_ptr<internal::Connection> connection) final
      __TA_EXCLUDES(lock_);
  void UnregisterConnection(internal::Connection* connection) final __TA_EXCLUDES(lock_);
  bool IsTerminating() const final;

  fbl::Mutex lock_;
  fbl::DoublyLinkedList<std::unique_ptr<internal::Connection>> connections_ __TA_GUARDED(lock_);

  std::atomic_bool is_shutting_down_;
  async::TaskMethod<ManagedVfs, &ManagedVfs::OnShutdownComplete> shutdown_task_ __TA_GUARDED(lock_){
      this};
  ShutdownCallback shutdown_handler_ __TA_GUARDED(lock_);
};

}  // namespace fs

#endif  // FS_MANAGED_VFS_H_
