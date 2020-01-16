// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/pseudo_dir/pseudo_dir_server.h"

#include <lib/async/cpp/task.h>

namespace modular {

PseudoDirServer::PseudoDirServer(std::unique_ptr<vfs::PseudoDir> pseudo_dir)
    : pseudo_dir_(std::move(pseudo_dir)),
      serving_thread_(
          [this](fidl::InterfaceRequest<fuchsia::io::Directory> request) {
            StartThread(std::move(request));
          },
          dir_.NewRequest()) {
  // Block until the run loop in |serving_thread_| is ready before returning
  // control to the caller.
  std::unique_lock<std::mutex> lock(ready_mutex_);
  thread_loop_ready_.wait(lock, [this] { return thread_loop_ != nullptr; });
}

PseudoDirServer::~PseudoDirServer() {
  FXL_CHECK(thread_loop_);
  // std::thread requires that we join() the thread before it is destroyed.
  thread_loop_->Quit();
  serving_thread_.join();
}

// Opens a read-only FD at |path|.  Path must not begin with '/'.
fbl::unique_fd PseudoDirServer::OpenAt(std::string path) {
  fuchsia::io::NodePtr node;
  dir_->Open(fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_FLAG_DESCRIBE,  // flags
             0u,                                                                  // mode
             path, node.NewRequest());

  return fsl::OpenChannelAsFileDescriptor(node.Unbind().TakeChannel());
}

fuchsia::io::DirectoryPtr PseudoDirServer::Serve() {
  fuchsia::io::DirectoryPtr directory;
  auto req = directory.NewRequest().TakeChannel();
  async::PostTask(thread_loop_->dispatcher(), [this, req = std::move(req)]() mutable {
    pseudo_dir_->Serve(fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
                       std::move(req));
  });
  return directory;
}

// This method is the handler for a new thread. It lets the owning thread know
// that it has started and serves a directory requests. The thread is exited
// when this object is destroyed.
void PseudoDirServer::StartThread(fidl::InterfaceRequest<fuchsia::io::Directory> request) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  {
    std::lock_guard<std::mutex> lock(ready_mutex_);

    // Setting this will let |thread_loop_ready_.wait()| proceed.
    thread_loop_ = &loop;

    pseudo_dir_->Serve(fuchsia::io::OPEN_RIGHT_READABLE, request.TakeChannel());
    thread_loop_ready_.notify_one();
  }

  thread_loop_->Run();
  // This thread exits when the owner thread calls thread_loop_->Quit().
}

}  // namespace modular
