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

#include <fbl/unique_fd.h>

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
  // Serves |pseudo_dir| on an async loop on a new thread. All requests to |pseudo_dir|
  // are processed on the new thread.
  PseudoDirServer(std::unique_ptr<vfs::PseudoDir> pseudo_dir);

  // Stops |loop_| and blocks the current thread until the |loop_| thread is finished.
  ~PseudoDirServer();

  // Opens a read-only FD at |path|.  |path| must not lead with a '/'.
  fbl::unique_fd OpenAt(std::string path);

  // Returns a new directory connection to |pseudo_dir_|.
  fuchsia::io::DirectoryPtr Serve();

  // Binds |request| to |pseudo_dir_|.
  void Serve(zx::channel request);

 private:
  // This loop is configured to attach to its own thread.
  async::Loop loop_;
  std::unique_ptr<vfs::PseudoDir> pseudo_dir_;

  // A directory connection, bound to |pseudo_dir_|, used by |OpenAt()|.
  fuchsia::io::DirectoryPtr dir_ptr_;
};

}  // namespace modular

#endif  // SRC_MODULAR_LIB_PSEUDO_DIR_PSEUDO_DIR_SERVER_H_
