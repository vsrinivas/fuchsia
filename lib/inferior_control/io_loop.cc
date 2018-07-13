// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "io_loop.h"

#include <unistd.h>

#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>

#include "garnet/lib/debugger_utils/util.h"
#include "lib/fsl/handles/object_info.h"
#include "lib/fxl/logging.h"

namespace debugserver {

IOLoop::IOLoop(int fd, Delegate* delegate, async::Loop* origin_loop)
    : quit_called_(false),
      fd_(fd),
      delegate_(delegate),
      is_running_(false),
      origin_loop_(origin_loop) {
  // Allow -1 for test purposes. This is a simple test anyway, the caller
  // could pass 314159 and we don't verify it's validity here.
  FXL_DCHECK(fd_ >= -1);
  FXL_DCHECK(delegate_);
  FXL_DCHECK(origin_loop_);
}

IOLoop::~IOLoop() { Quit(); }

void IOLoop::Run() {
  FXL_DCHECK(!is_running_);

  read_loop_.StartThread();
  write_loop_.StartThread();

  is_running_ = true;
  // Posts an asynchronous task on to listen for an incoming packet. This
  // initiates a loop that always reads for incoming packets. Called from
  // Run().
  async::PostTask(read_loop_.dispatcher(),
                  fit::bind_member(this, &IOLoop::OnReadTask));
}

void IOLoop::Quit() {
  FXL_DCHECK(is_running_);

  FXL_LOG(INFO) << "Quitting socket I/O loop";

  quit_called_ = true;

  read_loop_.Quit();
  write_loop_.Quit();
  read_loop_.Shutdown();
  write_loop_.Shutdown();

  FXL_LOG(INFO) << "Socket I/O loop exited";
}

void IOLoop::PostWriteTask(const fxl::StringView& bytes) {
  // We copy the data into the closure.
  // TODO(armansito): Pass a refptr/weaktpr to |this|?
  async::PostTask(write_loop_.dispatcher(), [this, bytes = bytes.ToString()] {
    ssize_t bytes_written = write(fd_, bytes.data(), bytes.size());

    // This cast isn't really safe, then again it should be virtually
    // impossible to send a large enough packet to cause an overflow (at
    // least with the GDB Remote protocol).
    if (bytes_written != static_cast<ssize_t>(bytes.size())) {
      FXL_LOG(ERROR) << "Failed to send bytes"
                     << ", " << ErrnoString(errno);
      ReportError();
      return;
    }
    FXL_VLOG(2) << "<- " << EscapeNonPrintableString(bytes);
  });
}

void IOLoop::ReportError() {
  // TODO(armansito): Pass a refptr/weaktpr to |this|?
  async::PostTask(origin_loop_->dispatcher(),
                  [this] { delegate_->OnIOError(); });
}

void IOLoop::ReportDisconnected() {
  // TODO(armansito): Pass a refptr/weaktpr to |this|?
  async::PostTask(origin_loop_->dispatcher(),
                  [this] { delegate_->OnDisconnected(); });
}

}  // namespace debugserver
