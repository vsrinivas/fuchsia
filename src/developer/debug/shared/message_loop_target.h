// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/thread.h>

#include "src/developer/debug/shared/message_loop.h"

namespace debug_ipc {

class ZirconExceptionWatcher;
class SocketWatcher;

enum class WatchType : uint32_t {
  kTask,
  kFdio,
  kProcessExceptions,
  kJobExceptions,
  kSocket
};
const char* WatchTypeToString(WatchType);

// MessageLoopTarget is an abstract interface for all message loops that can
// be used in the debug agent.
class MessageLoopTarget : public MessageLoop {
 public:
  // New message loops must be suscribed here.
  enum class Type {
    kAsync,
    kZircon,
    kLast,
  };
  static const char* TypeToString(Type);

  // Set by a message loop at InitTarget();
  static Type current_message_loop_type;

  // Target message loops can call wither Init or InitTarget. The difference is
  // that InitTarget will return a status about what happened, whether Init
  // will silently ignore it.
  virtual zx_status_t InitTarget() = 0;

  virtual ~MessageLoopTarget();

  virtual Type GetType() const = 0;

  // Fidl requires a special dispatcher to be setup. Not all message loops
  // support it.
  virtual bool SupportsFidl() const = 0;

  // Returns the current message loop or null if there isn't one.
  static MessageLoopTarget* Current();

  // MessageLoop implementation.
  WatchHandle WatchFD(WatchMode mode, int fd, FDWatcher* watcher) override = 0;

  // Watches the given socket for read/write status. The watcher must outlive
  // the returned WatchHandle. Must only be called on the message loop thread.
  //
  // The FDWatcher must not unregister from a callback. The handle might
  // become both readable and writable at the same time which will necessitate
  // calling both callbacks. The code does not expect the FDWatcher to
  // disappear in between these callbacks.
  virtual zx_status_t WatchSocket(WatchMode mode, zx_handle_t socket_handle,
                                  SocketWatcher* watcher, WatchHandle* out) = 0;

  // Attaches to the exception port of the given process and issues callbacks
  // on the given watcher. The watcher must outlive the returned WatchHandle.
  // Must only be called on the message loop thread.
  struct WatchProcessConfig {
    std::string process_name;
    zx_handle_t process_handle;
    zx_koid_t process_koid;
    ZirconExceptionWatcher* watcher = nullptr;
  };
  virtual zx_status_t WatchProcessExceptions(WatchProcessConfig config,
                                             WatchHandle* out) = 0;

  // Attaches to the exception port of the given job and issues callbacks
  // on the given watcher. The watcher must outlive the returned WatchHandle.
  // Must only be called on the message loop thread.
  struct WatchJobConfig {
    std::string job_name;
    zx_handle_t job_handle;
    zx_koid_t job_koid;
    ZirconExceptionWatcher* watcher;
  };
  virtual zx_status_t WatchJobExceptions(WatchJobConfig config,
                                         WatchHandle* out) = 0;

  // When this class issues an exception notification, the code should call
  // this function to resume the thread from the exception. This is a wrapper
  // for zx_task_resume_from_exception or it's async-loop equivalent.
  // |thread_koid| is needed to identify the exception in some message loop
  // implementations.
  virtual zx_status_t ResumeFromException(zx_koid_t thread_koid,
                                          zx::thread& thread,
                                          uint32_t options) = 0;
};

}  // namespace debug_ipc
