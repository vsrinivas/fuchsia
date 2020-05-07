// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/pseudo_dir/pseudo_dir_server.h"

#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>

namespace modular {

PseudoDirServer::PseudoDirServer(std::unique_ptr<vfs::PseudoDir> pseudo_dir)
    : loop_(&kAsyncLoopConfigNoAttachToCurrentThread), pseudo_dir_(std::move(pseudo_dir)) {
  FX_CHECK(loop_.StartThread() == ZX_OK);
  dir_ptr_ = Serve();
}

PseudoDirServer::~PseudoDirServer() {
  loop_.Quit();
  loop_.JoinThreads();
}

fbl::unique_fd PseudoDirServer::OpenAt(std::string path) {
  fuchsia::io::NodePtr node;
  dir_ptr_->Open(/*flags=*/fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_FLAG_DESCRIBE,
                 /*mode=*/0u, path, node.NewRequest());

  return fsl::OpenChannelAsFileDescriptor(node.Unbind().TakeChannel());
}

void PseudoDirServer::Serve(zx::channel request) {
  pseudo_dir_->Serve(fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
                     std::move(request), loop_.dispatcher());
}

fuchsia::io::DirectoryPtr PseudoDirServer::Serve() {
  fuchsia::io::DirectoryPtr directory;
  Serve(directory.NewRequest().TakeChannel());
  return directory;
}

}  // namespace modular
