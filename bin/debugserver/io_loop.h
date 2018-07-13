// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <array>

#include <lib/async-loop/cpp/loop.h>

#include "garnet/lib/inferior_control/io_loop.h"

namespace debugserver {

// This class implements IOLoop for Remote Serial Protocol support.

class RspIOLoop final : public inferior_control::IOLoop {
 public:
  RspIOLoop(int in_fd, Delegate* delegate, async::Loop* loop);

 private:
  // Maximum number of characters in the inbound buffer.
  constexpr static size_t kMaxBufferSize = 4096;

  void OnReadTask() override;

  // Buffer used for reading incoming bytes.
  std::array<char, kMaxBufferSize> in_buffer_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RspIOLoop);
};

}  // namespace debugserver
