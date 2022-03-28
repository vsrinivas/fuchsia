// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_GOLDFISH_PIPE_IO_PIPE_AUTO_READER_H_
#define SRC_DEVICES_LIB_GOLDFISH_PIPE_IO_PIPE_AUTO_READER_H_

#include <fuchsia/hardware/goldfish/pipe/cpp/banjo.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/zx/status.h>

#include <atomic>

#include "src/devices/lib/goldfish/pipe_io/pipe_io.h"

namespace goldfish {

// PipeAutoReader supports reading framed goldfish pipe messages automatically
// from the pipe. A message handler callback function is called on each
// pipe message received.
class PipeAutoReader : public PipeIo {
 public:
  using PipeMessageHandler = fit::function<void(PipeIo::ReadResult<char>)>;
  PipeAutoReader(ddk::GoldfishPipeProtocolClient* pipe, const char* pipe_name,
                 async_dispatcher_t* dispatcher, PipeMessageHandler handler = nullptr)
      : PipeIo(pipe, pipe_name), dispatcher_(dispatcher), handler_(std::move(handler)) {}

  void SetMessageHandler(PipeMessageHandler handler) { handler_ = std::move(handler); }

  // Start reading pipe messages asynchronously.
  // Returns:
  // - |ZX_ERR_BAD_STATE| if the dispatcher is shutting down.
  // - |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
  // - Otherwise, |ZX_OK|.
  zx_status_t BeginRead();

  // Cancel all reading tasks and pipe event wait jobs on the dispatcher.
  void StopRead();

 private:
  zx_status_t AsyncReadWithHeader();

  std::atomic<bool> running_ = false;
  async_dispatcher_t* dispatcher_;
  async::WaitOnce wait_event_;

  PipeMessageHandler handler_;
};

}  // namespace goldfish

#endif  // SRC_DEVICES_LIB_GOLDFISH_PIPE_IO_PIPE_AUTO_READER_H_
