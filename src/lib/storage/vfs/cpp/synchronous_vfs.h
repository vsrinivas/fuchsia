// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_SYNCHRONOUS_VFS_H_
#define SRC_LIB_STORAGE_VFS_CPP_SYNCHRONOUS_VFS_H_

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <lib/async/cpp/task.h>

#include <memory>

#include <fbl/intrusive_double_list.h>

#include "src/lib/storage/vfs/cpp/connection.h"
#include "src/lib/storage/vfs/cpp/fuchsia_vfs.h"

namespace fs {

// A specialization of |FuchsiaVfs| which tears down all active connections when it is destroyed.
//
// This class is NOT thread-safe and it must be used with a single-threaded asynchronous dispatcher.
//
// Additionally, this class must only be used with Vnode implementations that do not defer
// completion of operations.
//
// It is safe to shutdown the dispatch loop before destroying the SynchronousVfs object.
class SynchronousVfs : public FuchsiaVfs {
 public:
  SynchronousVfs();

  explicit SynchronousVfs(async_dispatcher_t* dispatcher);

  // The SynchronousVfs destructor terminates all open connections.
  ~SynchronousVfs() override;

  // FuchsiaVfs overrides.
  void CloseAllConnectionsForVnode(const Vnode& node,
                                   CloseAllConnectionsForVnodeCallback callback) final;
  bool IsTerminating() const final;

 private:
  // Synchronously drop all connections managed by the VFS.
  //
  // Invokes |handler| once when all connections are destroyed. It is safe to delete SynchronousVfs
  // from within the closure.
  void Shutdown(ShutdownCallback handler) override;

  zx_status_t RegisterConnection(std::unique_ptr<internal::Connection> connection,
                                 zx::channel channel) final;
  void UnregisterConnection(internal::Connection* connection) final;

  fbl::DoublyLinkedList<std::unique_ptr<internal::Connection>> connections_;
  bool is_shutting_down_;
};

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_SYNCHRONOUS_VFS_H_
