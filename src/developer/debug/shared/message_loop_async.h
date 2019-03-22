// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/developer/debug/shared/event_handlers.h"
#include "src/developer/debug/shared/message_loop_target.h"

#include <vector>

#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/unsafe.h>
#include <lib/zx/event.h>
#include <lib/zx/port.h>
#include <lib/zx/thread.h>

namespace debug_ipc {

class ExceptionHandler;
class SignalHandler;
class SocketWatcher;
class ZirconExceptionWatcher;

class MessageLoopAsync : public MessageLoopTarget {
 public:
  // Associated struct to track information about what type of resource a watch
  // handle is following.
  // EventHandlers need access to the WatchInfo implementation, hence the reason
  // for it to be public.
  // Definition at the end of the header.
  struct WatchInfo;

  using SignalHandlerMap = std::map<const async_wait_t*, SignalHandler>;
  using ExceptionHandlerMap =
      std::map<const async_exception_t*, ExceptionHandler>;

  MessageLoopAsync();
  ~MessageLoopAsync();

  void Init() override;
  zx_status_t InitTarget() override;

  Type GetType() const override { return Type::kAsync; }
  bool SupportsFidl() const override { return true; }

  void Cleanup() override;

  // Runs until timeout. Mostly used in tests.
  void RunUntilTimeout(zx::duration timeout);

  // Returns the current message loop or null if there isn't one.
  static MessageLoopAsync* Current();

  // MessageLoop implementation.
  WatchHandle WatchFD(WatchMode mode, int fd, FDWatcher* watcher) override;

  zx_status_t WatchSocket(WatchMode mode, zx_handle_t socket_handle,
                          SocketWatcher* watcher, WatchHandle* out) override;

  // Attaches to the exception port of the given process and issues callbacks
  // on the given watcher. The watcher must outlive the returned WatchHandle.
  // Must only be called on the message loop thread.
  zx_status_t WatchProcessExceptions(WatchProcessConfig config,
                                     WatchHandle* out) override;

  // Attaches to the exception port of the given job and issues callbacks
  // on the given watcher. The watcher must outlive the returned WatchHandle.
  // Must only be called on the message loop thread.
  zx_status_t WatchJobExceptions(WatchJobConfig config,
                                 WatchHandle* out) override;

  // When this class issues an exception notification, the code should call
  // this function to resume the thread from the exception. This is a wrapper
  // for zx_task_resume_from_exception.
  /* zx_status_t ResumeFromException(zx::thread& thread, uint32_t options); */
  zx_status_t ResumeFromException(zx_koid_t thread_koid, zx::thread&,
                                  uint32_t options) override;

  void QuitNow() override;

  const SignalHandlerMap& signal_handlers() const { return signal_handlers_; }

  const ExceptionHandlerMap& exception_handlers() const {
    return exception_handlers_;
  }

 private:
  const WatchInfo* FindWatchInfo(int id) const;

  // MessageLoop protected implementation.
  void RunImpl() override;
  void StopWatching(int id) override;
  // Triggers an event signaling that there is a pending event.
  void SetHasTasks() override;

  // Check for any pending C++ tasks and process them.
  // Returns true if there was an event pending to be processed.
  bool CheckAndProcessPendingTasks();

  // Handles WatchHandles event. These are all the events that are not C++ tasks
  // posted to the message loop.
  void HandleException(const ExceptionHandler&, zx_port_packet_t packet);

  // Handle an event of the given type.
  void OnFdioSignal(int watch_id, const WatchInfo& info, zx_signals_t observed);
  void OnProcessException(const ExceptionHandler&, const WatchInfo& info,
                          const zx_port_packet_t& packet);
  void OnProcessTerminated(const WatchInfo&, zx_signals_t observed);

  void OnJobException(const ExceptionHandler&, const WatchInfo& info,
                      const zx_port_packet_t& packet);
  void OnSocketSignal(int watch_id, const WatchInfo& info,
                      zx_signals_t observed);

  using WatchMap = std::map<int, WatchInfo>;
  WatchMap watches_;

  // ID used as an index into watches_.
  int next_watch_id_ = 1;

  async::Loop loop_;
  zx::event task_event_;

  SignalHandlerMap signal_handlers_;
  // See SignalHandler constructor.
  // |associated_info| needs to be updated with the fact that it has an
  // associated SignalHandler.
  zx_status_t AddSignalHandler(int, zx_handle_t, zx_signals_t, WatchInfo* info);
  void RemoveSignalHandler(const async_wait_t* id);

  ExceptionHandlerMap exception_handlers_;
  // See ExceptionHandler constructor.
  // |associated_info| needs to be updated with the fact that it has an
  // associated ExceptionHandler.
  zx_status_t AddExceptionHandler(int, zx_handle_t, uint32_t, WatchInfo* info);
  void RemoveExceptionHandler(const async_exception_t*);

  // Every exception source (ExceptionHandler) will get an async_exception_t*
  // that works as a "key" for the async_loop. This async_exception_t* is what
  // you give back to the loop to return from an exception.
  //
  // So, everytime there is an exception, there needs to be a tracking from
  // thread_koid to this async_exception_t*, so that when a thread is resumed,
  // we can pass to the loop the correct key by just using the thread koid.
  struct Exception;
  std::map<zx_koid_t, Exception> thread_exception_map_;
  void AddException(const ExceptionHandler&, zx_koid_t thread_koid);

  FXL_DISALLOW_COPY_AND_ASSIGN(MessageLoopAsync);

  friend class SignalHandler;
  friend class ExceptionHandler;
};

// EventHandlers need access to the WatchInfo implementation.
struct MessageLoopAsync::WatchInfo {
  // Name of the resource being watched.
  // Mostly tracked for debugging purposes.
  std::string resource_name;

  WatchType type = WatchType::kFdio;

  // FDIO-specific watcher parameters.
  int fd = -1;
  fdio_t* fdio = nullptr;
  FDWatcher* fd_watcher = nullptr;
  zx_handle_t fd_handle = ZX_HANDLE_INVALID;

  // Socket-specific parameters.
  SocketWatcher* socket_watcher = nullptr;
  zx_handle_t socket_handle = ZX_HANDLE_INVALID;

  // Task-exception-specific parameters, can be of job or process type.
  ZirconExceptionWatcher* exception_watcher = nullptr;
  zx_koid_t task_koid = 0;
  zx_handle_t task_handle = ZX_HANDLE_INVALID;

  // This makes easier the lookup of the associated ExceptionHandler with this
  // watch id.
  const async_wait_t* signal_handler_key = nullptr;
  const async_exception_t* exception_handler_key = nullptr;
};

}  // namespace debug_ipc
