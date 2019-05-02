// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/util/pseudo_dir_server.h"

namespace modular {

PseudoDirServer::PseudoDirServer(std::unique_ptr<vfs::PseudoDir> pseudo_dir)
    : pseudo_dir_(std::move(pseudo_dir)),
      serving_thread_(
          [this](fidl::InterfaceRequest<fuchsia::io::Directory> request) {
            StartThread(std::move(request));
          },
          dir_.NewRequest()) {
  // The thread (owned by |serving_thread_|) kicked off, but lets wait until
  // it's run loop is ready.
  std::unique_lock<std::mutex> lock(ready_mutex_);
  thread_loop_ready_.wait(lock);
}

PseudoDirServer::~PseudoDirServer() {
  FXL_CHECK(thread_loop_);
  // std::thread requires that we join() the thread before it is destroyed.
  thread_loop_->Quit();
  serving_thread_.join();
}

// Opens a read-only FD at |path|.  Path must not begin with '/'.
fxl::UniqueFD PseudoDirServer::OpenAt(std::string path) {
  fuchsia::io::NodePtr node;
  dir_->Open(fuchsia::io::OPEN_RIGHT_READABLE |
                 fuchsia::io::OPEN_FLAG_DESCRIBE,  // flags
             0u,                                   // mode
             path, node.NewRequest());

  return fsl::OpenChannelAsFileDescriptor(node.Unbind().TakeChannel());
}

// This method is the handler for a new thread. It lets the owning thread know
// that it has started and serves a directory requests. The thread is exited
// when this object is destroyed.
void PseudoDirServer::StartThread(
    fidl::InterfaceRequest<fuchsia::io::Directory> request) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  thread_loop_ = &loop;

  // The owner's thread is currently blocked waiting for this thread to
  // initialize a valid |thread_loop_|. Notify that thread that it's now safe
  // manipulate this thread's run loop:
  thread_loop_ready_.notify_all();

  pseudo_dir_->Serve(fuchsia::io::OPEN_RIGHT_READABLE, request.TakeChannel());
  thread_loop_->Run();
  // This thread exits when the owner thread calls thread_loop_->Quit().
}

}  // namespace modular
