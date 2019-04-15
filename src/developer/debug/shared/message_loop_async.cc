// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/message_loop_async.h"

#include <lib/fdio/io.h>
#include <lib/zx/handle.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <stdio.h>
#include <zircon/syscalls/exception.h>

#include "src/developer/debug/shared/event_handlers.h"
#include "src/developer/debug/shared/fd_watcher.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/socket_watcher.h"
#include "src/developer/debug/shared/zircon_exception_watcher.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/lib/fxl/compiler_specific.h"
#include "src/lib/fxl/logging.h"

namespace debug_ipc {

namespace {

thread_local MessageLoopAsync* current_message_loop_async = nullptr;

}  // namespace

// Exception ------------------------------------------------------------

struct MessageLoopAsync::Exception {
  zx_koid_t thread_koid = 0;
  // Not-owning. Must outlive.
  async_exception_t* exception_token = nullptr;
};

// MessageLoopAsync -----------------------------------------------------------

MessageLoopAsync::MessageLoopAsync() : loop_(&kAsyncLoopConfigAttachToThread) {}

MessageLoopAsync::~MessageLoopAsync() {
  FXL_DCHECK(Current() != this);  // Cleanup should have been called.
}

void MessageLoopAsync::Init() { InitTarget(); }

zx_status_t MessageLoopAsync::InitTarget() {
  MessageLoop::Init();

  FXL_DCHECK(!current_message_loop_async);
  current_message_loop_async = this;
  MessageLoopTarget::current_message_loop_type =
      MessageLoopTarget::Type::kAsync;

  zx::event::create(0, &task_event_);

  WatchInfo info;
  info.type = WatchType::kTask;
  zx_status_t status =
      AddSignalHandler(kTaskSignalKey, task_event_.get(), kTaskSignal, &info);

  if (status != ZX_OK)
    return status;

  watches_[kTaskSignalKey] = std::move(info);
  return ZX_OK;
}

void MessageLoopAsync::Cleanup() {
  // We need to remove the signal/exception handlers before the message loop
  // goes away.
  signal_handlers_.clear();
  exception_handlers_.clear();

  FXL_DCHECK(current_message_loop_async == this);
  current_message_loop_async = nullptr;
  MessageLoopTarget::current_message_loop_type = MessageLoopTarget::Type::kLast;

  MessageLoop::Cleanup();
}

// static
MessageLoopAsync* MessageLoopAsync::Current() {
  return current_message_loop_async;
}

const MessageLoopAsync::WatchInfo* MessageLoopAsync::FindWatchInfo(
    int id) const {
  auto it = watches_.find(id);
  if (it == watches_.end())
    return nullptr;
  return &it->second;
}

zx_status_t MessageLoopAsync::AddSignalHandler(int id, zx_handle_t object,
                                               zx_signals_t signals,
                                               WatchInfo* associated_info) {
  SignalHandler handler;
  zx_status_t status = handler.Init(id, object, signals);
  if (status != ZX_OK)
    return status;

  // The handler should not be there already.
  FXL_DCHECK(signal_handlers_.find(handler.handle()) == signal_handlers_.end());

  associated_info->signal_handler_key = handler.handle();
  signal_handlers_[handler.handle()] = std::move(handler);

  return ZX_OK;
}

zx_status_t MessageLoopAsync::AddExceptionHandler(int id, zx_handle_t object,
                                                  uint32_t options,
                                                  WatchInfo* associated_info) {
  ExceptionHandler handler;
  zx_status_t status = handler.Init(id, object, options);
  if (status != ZX_OK)
    return status;

  // The handler should not be there already.
  FXL_DCHECK(exception_handlers_.find(handler.handle()) ==
             exception_handlers_.end());

  associated_info->exception_handler_key = handler.handle();
  exception_handlers_[handler.handle()] = std::move(handler);

  return ZX_OK;
}

MessageLoop::WatchHandle MessageLoopAsync::WatchFD(WatchMode mode, int fd,
                                                   FDWatcher* watcher) {
  WatchInfo info;
  info.type = WatchType::kFdio;
  info.fd_watcher = watcher;
  info.fd = fd;
  info.fdio = fdio_unsafe_fd_to_io(fd);
  if (!info.fdio)
    return WatchHandle();

  uint32_t events = 0;
  switch (mode) {
    case WatchMode::kRead:
      events = POLLIN;
      break;
    case WatchMode::kWrite:
      events = POLLOUT;
      break;
    case WatchMode::kReadWrite:
      events = POLLIN | POLLOUT;
      break;
  }

  zx_signals_t signals = ZX_SIGNAL_NONE;
  fdio_unsafe_wait_begin(info.fdio, events, &info.fd_handle, &signals);
  if (info.fd_handle == ZX_HANDLE_INVALID)
    return WatchHandle();

  int watch_id;
  {
    std::lock_guard<std::mutex> guard(mutex_);

    watch_id = next_watch_id_;
    next_watch_id_++;
  }

  zx_status_t status =
      AddSignalHandler(watch_id, info.fd_handle, signals, &info);
  if (status != ZX_OK)
    return WatchHandle();

  watches_[watch_id] = info;
  return WatchHandle(this, watch_id);
}

zx_status_t MessageLoopAsync::WatchSocket(WatchMode mode,
                                          zx_handle_t socket_handle,
                                          SocketWatcher* watcher,
                                          MessageLoop::WatchHandle* out) {
  WatchInfo info;
  info.type = WatchType::kSocket;
  info.socket_watcher = watcher;
  info.socket_handle = socket_handle;

  int watch_id;
  {
    std::lock_guard<std::mutex> guard(mutex_);

    watch_id = next_watch_id_;
    next_watch_id_++;
  }

  zx_signals_t signals = 0;
  if (mode == WatchMode::kRead || mode == WatchMode::kReadWrite)
    signals |= ZX_SOCKET_READABLE;

  if (mode == WatchMode::kWrite || mode == WatchMode::kReadWrite)
    signals |= ZX_SOCKET_WRITABLE;

  zx_status_t status =
      AddSignalHandler(watch_id, socket_handle, ZX_SOCKET_WRITABLE, &info);
  if (status != ZX_OK)
    return status;

  watches_[watch_id] = info;
  *out = WatchHandle(this, watch_id);
  return ZX_OK;
}

zx_status_t MessageLoopAsync::WatchProcessExceptions(
    WatchProcessConfig config, MessageLoop::WatchHandle* out) {
  WatchInfo info;
  info.resource_name = config.process_name;
  info.type = WatchType::kProcessExceptions;
  info.exception_watcher = config.watcher;
  info.task_koid = config.process_koid;
  info.task_handle = config.process_handle;

  int watch_id;
  {
    std::lock_guard<std::mutex> guard(mutex_);

    watch_id = next_watch_id_;
    next_watch_id_++;
  }

  // Watch all exceptions for the process.
  zx_status_t status;
  status = AddExceptionHandler(watch_id, config.process_handle,
                               ZX_EXCEPTION_PORT_DEBUGGER, &info);
  if (status != ZX_OK)
    return status;

  // Watch for the process terminated signal.
  status = AddSignalHandler(watch_id, config.process_handle,
                            ZX_PROCESS_TERMINATED, &info);
  if (status != ZX_OK)
    return status;

  DEBUG_LOG(MessageLoop) << "Watching process " << info.resource_name;

  watches_[watch_id] = info;
  *out = WatchHandle(this, watch_id);
  return ZX_OK;
}

zx_status_t MessageLoopAsync::WatchJobExceptions(
    WatchJobConfig config, MessageLoop::WatchHandle* out) {
  WatchInfo info;
  info.resource_name = config.job_name;
  info.type = WatchType::kJobExceptions;
  info.exception_watcher = config.watcher;
  info.task_koid = config.job_koid;
  info.task_handle = config.job_handle;

  int watch_id;
  {
    std::lock_guard<std::mutex> guard(mutex_);

    watch_id = next_watch_id_;
    next_watch_id_++;
  }

  // Create and track the exception handle.
  zx_status_t status = AddExceptionHandler(watch_id, config.job_handle,
                                           ZX_EXCEPTION_PORT_DEBUGGER, &info);
  if (status != ZX_OK)
    return status;

  DEBUG_LOG(MessageLoop) << "Watching job " << info.resource_name;

  watches_[watch_id] = info;
  *out = WatchHandle(this, watch_id);
  return ZX_OK;
}

zx_status_t MessageLoopAsync::ResumeFromException(zx_koid_t thread_koid,
                                                  zx::thread& thread,
                                                  uint32_t options) {
  auto it = thread_exception_map_.find(thread_koid);
  FXL_DCHECK(it != thread_exception_map_.end());
  zx_status_t res = async_resume_from_exception(async_get_default_dispatcher(),
                                                it->second.exception_token,
                                                thread.get(), options);
  thread_exception_map_.erase(thread_koid);
  return res;
}

bool MessageLoopAsync::CheckAndProcessPendingTasks() {
  std::lock_guard<std::mutex> guard(mutex_);
  // Do a C++ task.
  if (ProcessPendingTask()) {
    SetHasTasks();  // Enqueue another task signal.
    return true;
  }
  return false;
}

void MessageLoopAsync::HandleException(const ExceptionHandler& handler,
                                       zx_port_packet_t packet) {
  WatchInfo* watch_info = nullptr;
  {
    // Some event being watched.
    std::lock_guard<std::mutex> guard(mutex_);
    auto found = watches_.find(packet.key);
    if (found == watches_.end()) {
      // It is possible to get an exception that doesn't have a watch handle.
      // A case is a race between detaching from a process and getting an
      // exception on that process.
      //
      // The normal process looks like this:
      //
      // 1. In order to correctly detach, the debug agent has to resume threads
      //    from their exceptions. Otherwise that exception will be treated as
      //    unhandled when the agent detaches and will bubble up.
      // 2. The agent detaches from the exception port. This means that the
      //    watch handle is no longer listening.
      //
      // It is possible between (1) and (2) to get an exception, which will be
      // queued in the exception port of the thread. Now, the agent won't read
      // from the port until *after* it has detached from the exception port.
      // This means that this new exception is not handled and will be bubbled
      // up, which is correct as the debug agent stated that it has nothing more
      // to do with the process.
      //
      // Now the problem is that zircon does not clean stale packets from a
      // queue, meaning that the next time the message loop waits on the port,
      // it will find a stale packet. In this context a stale packet means one
      // that does not have a watch handle, as it was deleted in (1). Hence we
      // get into this case and we simply log it for posperity.
      //
      // TODO(ZX-2623): zircon is going to clean up stale packets from ports
      //                in the future. When that is done, this case should not
      //                happen and we should go back into asserting it.
      FXL_LOG(WARNING) << "Got stale port packet. This is most probably due to "
                          "a race between detaching from a process and an "
                          "exception ocurring.";
      return;
    }
    watch_info = &found->second;
  }

  // Dispatch the watch callback outside of the lock. This depends on the
  // stability of the WatchInfo pointer in the map (std::map is stable across
  // updates) and the watch not getting unregistered from another thread
  // asynchronously (which the API requires and is enforced by a DCHECK in
  // the StopWatching impl).
  switch (watch_info->type) {
    case WatchType::kProcessExceptions:
      OnProcessException(handler, *watch_info, packet);
      break;
    case WatchType::kJobExceptions:
      OnJobException(handler, *watch_info, packet);
      break;
    case WatchType::kTask:
    case WatchType::kFdio:
    case WatchType::kSocket:
      FXL_NOTREACHED();
  }
}

uint64_t MessageLoopAsync::GetMonotonicNowNS() const {
  zx::time ret;
  zx::clock::get(&ret);

  return ret.get();
}

// Previously, the approach was to first look for C++ tasks and when handled
// look for WatchHandle work and finally wait for an event. This worked because
// handle events didn't post C++ tasks.
//
// But some tests do post tasks on handle events. Because C++ tasks are signaled
// by explicitly signaling an zx::event, without manually checking, the C++
// tasks will never be checked and we would get blocked until a watch handled
// is triggered.
//
// In order to handle the events properly, we need to check for C++ tasks before
// and *after* handling watch handle events. This way we always process C++
// tasks before handle events and will get signaled if one of them posted a new
// task.
void MessageLoopAsync::RunImpl() {
  // Init should have been called.
  FXL_DCHECK(Current() == this);
  zx_status_t status;

  zx::time time;
  uint64_t delay = DelayNS();
  if (delay == MessageLoop::kMaxDelay) {
    time = zx::time::infinite();
  } else {
    time = zx::deadline_after(zx::nsec(delay));
  }

  for (;;) {
    status = loop_.ResetQuit();
    FXL_DCHECK(status != ZX_ERR_BAD_STATE);
    status = loop_.Run(time);
    FXL_DCHECK(status == ZX_OK || status == ZX_ERR_CANCELED ||
               status == ZX_ERR_TIMED_OUT)
        << "Expected ZX_OK || ZX_ERR_CANCELED || ZX_ERR_TIMED_OUT, got "
        << ZxStatusToString(status);

    if (status != ZX_ERR_TIMED_OUT) {
      return;
    }

    std::lock_guard<std::mutex> guard(mutex_);
    if (ProcessPendingTask())
      SetHasTasks();
  }
}

void MessageLoopAsync::QuitNow() {
  MessageLoop::QuitNow();
  loop_.Quit();
}

void MessageLoopAsync::StopWatching(int id) {
  // The dispatch code for watch callbacks requires this be called on the
  // same thread as the message loop is.
  FXL_DCHECK(Current() == this);

  std::lock_guard<std::mutex> guard(mutex_);

  auto found = watches_.find(id);
  FXL_DCHECK(found != watches_.end());

  WatchInfo& info = found->second;
  // BufferedFD constantly creates and destroys FD handles, flooding the log
  // with non-helpful logging statements.
  if (info.type != WatchType::kFdio) {
    DEBUG_LOG(MessageLoop) << "Stop watching " << WatchTypeToString(info.type)
                           << " " << info.resource_name;
  }

  switch (info.type) {
    case WatchType::kProcessExceptions: {
      RemoveExceptionHandler(info.exception_handler_key);
      RemoveSignalHandler(info.signal_handler_key);
      break;
    }
    case WatchType::kJobExceptions: {
      RemoveExceptionHandler(info.exception_handler_key);
      break;
    }
    case WatchType::kTask:
    case WatchType::kFdio:
    case WatchType::kSocket:
      RemoveSignalHandler(info.signal_handler_key);
      break;
  }
  watches_.erase(found);
}

void MessageLoopAsync::SetHasTasks() { task_event_.signal(0, kTaskSignal); }

void MessageLoopAsync::OnFdioSignal(int watch_id, const WatchInfo& info,
                                    zx_signals_t observed) {
  uint32_t events = 0;
  fdio_unsafe_wait_end(info.fdio, observed, &events);

  if ((events & POLLERR) || (events & POLLHUP) || (events & POLLNVAL) ||
      (events & POLLRDHUP)) {
    info.fd_watcher->OnFDReady(info.fd, false, false, true);

    // Don't dispatch any other notifications when there's an error. Zircon
    // seems to set readable and writable on error even if there's nothing
    // there.
    return;
  }

  bool readable = !!(events & POLLIN);
  bool writable = !!(events & POLLOUT);
  info.fd_watcher->OnFDReady(info.fd, readable, writable, false);
}

void MessageLoopAsync::RemoveSignalHandler(const async_wait_t* key) {
  FXL_DCHECK(key);
  size_t erase_count = signal_handlers_.erase(key);
  FXL_DCHECK(erase_count == 1u);
}

void MessageLoopAsync::RemoveExceptionHandler(const async_exception_t* key) {
  FXL_DCHECK(key);
  size_t erase_count = exception_handlers_.erase(key);
  FXL_DCHECK(erase_count == 1u);
}

void MessageLoopAsync::AddException(const ExceptionHandler& handler,
                                    zx_koid_t thread_koid) {
  FXL_DCHECK(thread_exception_map_.find(thread_koid) ==
             thread_exception_map_.end());

  Exception exception;
  exception.thread_koid = thread_koid;
  exception.exception_token = const_cast<async_exception_t*>(handler.handle());
  thread_exception_map_[thread_koid] = std::move(exception);
}

void MessageLoopAsync::OnProcessException(const ExceptionHandler& handler,
                                          const WatchInfo& info,
                                          const zx_port_packet_t& packet) {
  if (ZX_PKT_IS_EXCEPTION(packet.type)) {
    // All debug exceptions.
    switch (packet.type) {
      case ZX_EXCP_THREAD_STARTING:
        AddException(handler, packet.exception.tid);
        info.exception_watcher->OnThreadStarting(info.task_koid,
                                                 packet.exception.tid);
        break;
      case ZX_EXCP_THREAD_EXITING:
        AddException(handler, packet.exception.tid);
        info.exception_watcher->OnThreadExiting(info.task_koid,
                                                packet.exception.tid);
        break;
      case ZX_EXCP_GENERAL:
      case ZX_EXCP_FATAL_PAGE_FAULT:
      case ZX_EXCP_UNDEFINED_INSTRUCTION:
      case ZX_EXCP_SW_BREAKPOINT:
      case ZX_EXCP_HW_BREAKPOINT:
      case ZX_EXCP_UNALIGNED_ACCESS:
      case ZX_EXCP_POLICY_ERROR:
        AddException(handler, packet.exception.tid);
        info.exception_watcher->OnException(info.task_koid,
                                            packet.exception.tid, packet.type);
        break;
      default:
        FXL_NOTREACHED();
    }
  } else {
    FXL_NOTREACHED();
  }
}

void MessageLoopAsync::OnProcessTerminated(const WatchInfo& info,
                                           zx_signals_t observed) {
  FXL_DCHECK(observed & ZX_PROCESS_TERMINATED);
  info.exception_watcher->OnProcessTerminated(info.task_koid);
}

void MessageLoopAsync::OnJobException(const ExceptionHandler& handler,
                                      const WatchInfo& info,
                                      const zx_port_packet_t& packet) {
  if (ZX_PKT_IS_EXCEPTION(packet.type)) {
    // All debug exceptions.
    switch (packet.type) {
      case ZX_EXCP_PROCESS_STARTING:
        AddException(handler, packet.exception.tid);
        info.exception_watcher->OnProcessStarting(
            info.task_koid, packet.exception.pid, packet.exception.tid);
        return;
      default:
        break;
    }
  }
  FXL_NOTREACHED();
}

void MessageLoopAsync::OnSocketSignal(int watch_id, const WatchInfo& info,
                                      zx_signals_t observed) {
  // Dispatch readable signal.
  if (observed & ZX_SOCKET_READABLE)
    info.socket_watcher->OnSocketReadable(info.socket_handle);

  // When signaling both readable and writable, make sure the readable handler
  // didn't remove the watch.
  if ((observed & ZX_SOCKET_READABLE) && (observed & ZX_SOCKET_WRITABLE)) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (watches_.find(watch_id) == watches_.end())
      return;
  }

  // Dispatch writable signal.
  if (observed & ZX_SOCKET_WRITABLE)
    info.socket_watcher->OnSocketWritable(info.socket_handle);
}

}  // namespace debug_ipc
