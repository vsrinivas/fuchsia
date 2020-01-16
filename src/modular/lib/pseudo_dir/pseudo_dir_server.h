// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_PSEUDO_DIR_PSEUDO_DIR_SERVER_H_
#define SRC_MODULAR_LIB_PSEUDO_DIR_PSEUDO_DIR_SERVER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/vfs/cpp/pseudo_dir.h>

#include <condition_variable>
#include <mutex>
#include <thread>

#include <src/lib/files/unique_fd.h>

#include "src/lib/fsl/io/fd.h"

namespace modular {

// Given a pseudo directory, spins up a thread and serves Directory operations
// over it. This utility is useful for making thread-blocking posix calls to the
// given PseudoDir, which needs its owning thread to not be blocked to service
// directory calls.
//
// The directory is accessible using |OpenAt()|.
//
// This class is thread-unsafe.
class PseudoDirServer final {
 public:
  // Spins up a thread to serve the given |pseudo_dir| directory calls over.
  // This constructor blocks the current thread until the child thread has
  // initialized.
  //
  // Requires that the calling thread has an async dispatcher.
  PseudoDirServer(std::unique_ptr<vfs::PseudoDir> pseudo_dir);

  // This destructor blocks the current thread until the child thread exits.
  ~PseudoDirServer();

  // Opens a read-only FD at |path|.  |path| must not lead with a '/'.
  fbl::unique_fd OpenAt(std::string path);

  // Returns a directory connection for this pseudo dir. This directory is
  // served on a different thread than the caller's thread.
  fuchsia::io::DirectoryPtr Serve();

 private:
  // This method is the handler for a new thread. It lets the owning thread
  // know that it has started and serves a directory requests. The thread is
  // exited when this object is destroyed.
  void StartThread(fidl::InterfaceRequest<fuchsia::io::Directory> request);

  std::unique_ptr<vfs::PseudoDir> pseudo_dir_;

  // The directory connection which |OpenAt()| uses. This directory connection
  // is served on |serving_thread_|'s thread.
  fuchsia::io::DirectoryPtr dir_;

  // The mutex & condition variable are used by the new thread (owned by
  // |serving_thread_|) to signal to the owning thread that it has started,
  // making it safe to then access |thread_loop_|.
  std::mutex ready_mutex_;
  std::condition_variable thread_loop_ready_;
  async::Loop* thread_loop_ = nullptr;  // serving thread's loop.

  std::thread serving_thread_;
};

}  // namespace modular

#endif  // SRC_MODULAR_LIB_PSEUDO_DIR_PSEUDO_DIR_SERVER_H_
