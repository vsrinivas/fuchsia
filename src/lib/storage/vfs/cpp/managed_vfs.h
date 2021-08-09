// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_MANAGED_VFS_H_
#define SRC_LIB_STORAGE_VFS_CPP_MANAGED_VFS_H_

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <lib/async/cpp/task.h>
#include <lib/fit/defer.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <fbl/intrusive_double_list.h>

#include "src/lib/storage/vfs/cpp/connection.h"
#include "src/lib/storage/vfs/cpp/vfs.h"

namespace fs {

// A specialization of |Vfs| which tracks FIDL connections. and integrates them with Vnode requests.
// This implementation is the normal one used on Fuchsia. It will not work in host builds.
//
// This class is thread-safe, but it is unsafe to shutdown the dispatch loop before shutting down
// the ManagedVfs object.
class ManagedVfs : public Vfs {
 public:
  explicit ManagedVfs(async_dispatcher_t* dispatcher);

  // The ManagedVfs destructor is only safe to execute if no connections are actively registered.
  //
  // To ensure that this state is achieved, it is recommended that clients issue a call to
  // |Shutdown| before calling the destructor.
  ~ManagedVfs() override;

  // Asynchronously drop all connections managed by the VFS.
  //
  // Invokes |handler| once when all connections are destroyed. It is safe to delete ManagedVfs from
  // within the closure.
  //
  // It is unsafe to call Shutdown multiple times.
  void Shutdown(ShutdownCallback handler) override __TA_EXCLUDES(lock_);

  void CloseAllConnectionsForVnode(const Vnode& node,
                                   CloseAllConnectionsForVnodeCallback callback) final;

 private:
  // Posts the task for OnShutdownComplete if it is safe to do so.
  void CheckForShutdownComplete() __TA_REQUIRES(lock_);

  // Identifies if the filesystem has fully terminated, and is ready for "OnShutdownComplete" to
  // execute.
  bool IsTerminated() const __TA_REQUIRES(lock_);

  // Invokes the handler from |Shutdown| once all connections have been released. Additionally,
  // unmounts all sub-mounted filesystems, if any exist.
  void OnShutdownComplete(async_dispatcher_t*, async::TaskBase*, zx_status_t status)
      __TA_EXCLUDES(lock_);

  zx_status_t RegisterConnection(std::unique_ptr<internal::Connection> connection,
                                 zx::channel channel) final __TA_EXCLUDES(lock_);
  void UnregisterConnection(internal::Connection* connection) final __TA_EXCLUDES(lock_);
  bool IsTerminating() const final;

  std::mutex lock_;

  // All live connections. There can be more than one connection per node.
  fbl::DoublyLinkedList<std::unique_ptr<internal::Connection>> connections_ __TA_GUARDED(lock_);

  std::atomic_bool is_shutting_down_ = false;
  async::TaskMethod<ManagedVfs, &ManagedVfs::OnShutdownComplete> shutdown_task_ __TA_GUARDED(lock_){
      this};
  ShutdownCallback shutdown_handler_ __TA_GUARDED(lock_);

  std::unordered_map<internal::Connection*,
                     std::shared_ptr<fit::deferred_action<fit::callback<void()>>>>
      closing_connections_ __TA_GUARDED(lock_);
};

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_MANAGED_VFS_H_
