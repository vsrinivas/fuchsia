// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_SYNCHRONOUS_VFS_H_
#define FS_SYNCHRONOUS_VFS_H_

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <lib/async/cpp/task.h>

#include <memory>

#include <fbl/function.h>
#include <fbl/intrusive_double_list.h>
#include <fs/internal/connection.h>
#include <fs/vfs.h>

namespace fs {

// A specialization of |Vfs| which tears down all active connections when it
// is destroyed.
//
// This class is NOT thread-safe and it must be used with a
// single-threaded asynchronous dispatcher.
//
// Additionally, this class must only be used with Vnode implementations
// that do not defer completion of operations; "deferred callback" closures
// must be invoked before returning "ERR_DISPATCHER_ASYNC".
//
// It is safe to shutdown the dispatch loop before destroying the
// SynchronousVfs object.
class SynchronousVfs : public Vfs {
 public:
  SynchronousVfs();
  SynchronousVfs(async_dispatcher_t* dispatcher);

  // The SynchronousVfs destructor terminates all open
  // connections.
  ~SynchronousVfs() override;

 private:
  // Synchronously drop all connections managed by the VFS.
  //
  // Invokes |handler| once when all connections are destroyed.
  // It is safe to delete SynchronousVfs from within the closure.
  void Shutdown(ShutdownCallback handler) override;

  void RegisterConnection(std::unique_ptr<internal::Connection> connection) final;
  void UnregisterConnection(internal::Connection* connection) final;
  bool IsTerminating() const final;

  fbl::DoublyLinkedList<std::unique_ptr<internal::Connection>> connections_;
  bool is_shutting_down_;
};

}  // namespace fs

#endif  // FS_SYNCHRONOUS_VFS_H_
