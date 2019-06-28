// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_EVENT_HANDLERS_H_
#define SRC_DEVELOPER_DEBUG_SHARED_EVENT_HANDLERS_H_

#include <lib/async/cpp/exception.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/channel.h>

#include <memory>

#include "src/lib/fxl/macros.h"

// Group of classes dedicated at handling async events associated with zircon's message loop.

namespace debug_ipc {

enum class WatchType : uint32_t;

// This signal on the task_event_ indicates there is work to do.
constexpr uint32_t kTaskSignal = ZX_USER_SIGNAL_0;

// 0 is an invalid ID for watchers, so is safe to use here.
constexpr uint64_t kTaskSignalKey = 0;

// Function called when a SignalHandler gets a signal it's waiting for.
using SignalHandlerFunc = void (*)(async_dispatcher_t*, async_wait_t*, zx_status_t,
                                   const zx_packet_signal_t*);

class SignalHandler {
 public:
  static void Handler(async_dispatcher_t*, async_wait_t*, zx_status_t, const zx_packet_signal_t*);

  SignalHandler();
  ~SignalHandler();

  FXL_DISALLOW_COPY_AND_ASSIGN(SignalHandler);
  SignalHandler(SignalHandler&&);
  SignalHandler& operator=(SignalHandler&&);

  zx_status_t Init(int id, zx_handle_t object, zx_signals_t signals);

  int watch_info_id() const { return watch_info_id_; }
  const async_wait_t* handle() const { return handle_.get(); }

 private:
  int watch_info_id_ = -1;
  std::unique_ptr<async_wait_t> handle_;
};

// This is the exception handler that uses exception token instead of the deprecated exception
// ports.
class ChannelExceptionHandler {
 public:
  static void Handler(async_dispatcher_t*, async_wait_t*, zx_status_t, const zx_packet_signal_t*);

  ChannelExceptionHandler();
  ~ChannelExceptionHandler();

  ChannelExceptionHandler(ChannelExceptionHandler&&);
  ChannelExceptionHandler& operator=(ChannelExceptionHandler&&);

  zx_status_t Init(int id, zx_handle_t object, uint32_t options);

  int watch_info_id() const { return watch_info_id_; }
  const async_wait_t* handle() const { return handle_.get(); }

 private:
  int watch_info_id_ = -1;
  std::unique_ptr<async_wait_t> handle_;

  zx::channel exception_channel_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ChannelExceptionHandler);
};

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_SHARED_EVENT_HANDLERS_H_
