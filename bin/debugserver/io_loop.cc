// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "io_loop.h"

#include <unistd.h>

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "garnet/lib/debugger_utils/util.h"
#include "lib/fxl/logging.h"

namespace debugserver {

RspIOLoop::RspIOLoop(int in_fd, Delegate* delegate, async::Loop* loop)
    : IOLoop(in_fd, delegate, loop) {
}

void RspIOLoop::OnReadTask() {
  FXL_DCHECK(async_get_default_dispatcher() == read_dispatcher());

  ssize_t read_size = read(fd(), in_buffer_.data(), kMaxBufferSize);

  // 0 bytes means that the remote end closed the TCP connection.
  if (read_size == 0) {
    FXL_VLOG(1) << "Client closed connection";
    ReportDisconnected();
    return;
  }

  // There was an error
  if (read_size < 0) {
    FXL_LOG(ERROR) << "Error occurred while waiting for a packet" << ", "
                   << util::ErrnoString(errno);
    ReportError();
    return;
  }

  fxl::StringView bytes_read(in_buffer_.data(), read_size);
  FXL_VLOG(2) << "-> " << util::EscapeNonPrintableString(bytes_read);

  // Notify the delegate that we read some bytes. We copy the buffer data
  // into the closure as |in_buffer_| can get modified before the closure
  // runs.
  // TODO(armansito): Pass a weakptr to |delegate_|?
  async::PostTask(origin_dispatcher(), [ bytes_read = bytes_read.ToString(), this ] {
    delegate()->OnBytesRead(bytes_read);
  });

  if (!quit_called()) {
    async::PostTask(read_dispatcher(), std::bind(&RspIOLoop::OnReadTask, this));
  }
}

}  // namespace debugserver
