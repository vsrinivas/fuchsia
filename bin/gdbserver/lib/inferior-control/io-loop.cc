// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "io-loop.h"

#include <unistd.h>

#include "lib/fxl/logging.h"
#include "lib/mtl/handles/object_info.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/threading/create_thread.h"

#include "debugger-utils/util.h"

namespace debugserver {

IOLoop::IOLoop(int fd, Delegate* delegate)
    : quit_called_(false), fd_(fd), delegate_(delegate), is_running_(false) {
  FXL_DCHECK(fd_ >= 0);
  FXL_DCHECK(delegate_);
  FXL_DCHECK(mtl::MessageLoop::GetCurrent());

  origin_task_runner_ = mtl::MessageLoop::GetCurrent()->task_runner();
}

IOLoop::~IOLoop() {
  Quit();
}

void IOLoop::Run() {
  FXL_DCHECK(!is_running_);

  is_running_ = true;
  read_thread_ = mtl::CreateThread(&read_task_runner_, "i/o loop read task");
  write_thread_ = mtl::CreateThread(&write_task_runner_, "i/o loop write task");

  StartReadLoop();
}

void IOLoop::Quit() {
  FXL_DCHECK(is_running_);

  FXL_LOG(INFO) << "Quitting socket I/O loop";

  quit_called_ = true;

  auto quit_task = [] {
    // Tell the thread-local message loop to quit.
    FXL_DCHECK(mtl::MessageLoop::GetCurrent());
    mtl::MessageLoop::GetCurrent()->QuitNow();
  };
  read_task_runner_->PostTask(quit_task);
  write_task_runner_->PostTask(quit_task);

  if (read_thread_.joinable())
    read_thread_.join();
  if (write_thread_.joinable())
    write_thread_.join();

  FXL_LOG(INFO) << "Socket I/O loop exited";
}

void IOLoop::StartReadLoop() {
  // Make sure the call is coming from the origin thread.
  FXL_DCHECK(mtl::MessageLoop::GetCurrent()->task_runner().get() ==
             origin_task_runner_.get());

  read_task_runner_->PostTask(std::bind(&IOLoop::OnReadTask, this));
}

void IOLoop::PostWriteTask(const fxl::StringView& bytes) {
  // We copy the data into the closure.
  // TODO(armansito): Pass a refptr/weaktpr to |this|?
  write_task_runner_->PostTask([ this, bytes = bytes.ToString() ] {
    ssize_t bytes_written = write(fd_, bytes.data(), bytes.size());

    // This cast isn't really safe, then again it should be virtually
    // impossible to send a large enough packet to cause an overflow (at
    // least with the GDB Remote protocol).
    if (bytes_written != static_cast<ssize_t>(bytes.size())) {
      FXL_LOG(ERROR) << "Failed to send bytes" << ", "
                     << util::ErrnoString(errno);
      ReportError();
      return;
    }
    FXL_VLOG(2) << "<- " << util::EscapeNonPrintableString(bytes);
  });
}

void IOLoop::ReportError() {
  // TODO(armansito): Pass a refptr/weakptr to |this|?
  origin_task_runner_->PostTask([this] { delegate_->OnIOError(); });
}

void IOLoop::ReportDisconnected() {
  // TODO(armansito): Pass a refptr/weakptr to |this|?
  origin_task_runner_->PostTask([this] { delegate_->OnDisconnected(); });
}

}  // namespace debugserver
