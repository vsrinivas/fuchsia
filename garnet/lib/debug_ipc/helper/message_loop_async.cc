// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/helper/message_loop_async.h"

#include <stdio.h>

#include <lib/fdio/io.h>
#include <lib/zx/handle.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <zircon/syscalls/exception.h>

#include "garnet/lib/debug_ipc/debug/block_timer.h"
#include "garnet/lib/debug_ipc/helper/event_handlers.h"
#include "garnet/lib/debug_ipc/helper/fd_watcher.h"
#include "garnet/lib/debug_ipc/helper/socket_watcher.h"
#include "garnet/lib/debug_ipc/helper/zircon_exception_watcher.h"
#include "garnet/lib/debug_ipc/helper/zx_status.h"
#include "garnet/public/lib/fxl/compiler_specific.h"
#include "garnet/public/lib/fxl/logging.h"

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

void MessageLoopAsync::Init() {
  MessageLoop::Init();

  FXL_DCHECK(!current_message_loop_async);
  current_message_loop_async = this;
  MessageLoopTarget::current_message_loop_type =
      MessageLoopTarget::LoopType::kAsync;

  zx::event::create(0, &task_event_);

  WatchInfo info;
  info.type = WatchType::kTask;
  AddSignalHandler(kTaskSignalKey, task_event_.get(), kTaskSignal, &info);
  watches_[kTaskSignalKey] = info;
}

void MessageLoopAsync::Cleanup() {
  // We need to remove the signal/exception handlers before the message loop
  // goes away.
  signal_handlers_.clear();
  exception_handlers_.clear();

  FXL_DCHECK(current_message_loop_async == this);
  current_message_loop_async = nullptr;
  MessageLoopTarget::current_message_loop_type =
      MessageLoopTarget::LoopType::kLast;

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

void MessageLoopAsync::AddSignalHandler(int id, zx_handle_t object,
                                        zx_signals_t signals,
                                        WatchInfo* associated_info) {
  SignalHandler handler(id, object, signals);

  // The handler should not be there already.
  FXL_DCHECK(signal_handlers_.find(handler.handle()) == signal_handlers_.end());

  associated_info->signal_handler_key = handler.handle();
  signal_handlers_[handler.handle()] = std::move(handler);
}

void MessageLoopAsync::AddExceptionHandler(int id, zx_handle_t object,
                                           uint32_t options,
                                           WatchInfo* associated_info) {
  ExceptionHandler handler(id, object, options);

  // The handler should not be there already.
  FXL_DCHECK(exception_handlers_.find(handler.handle()) ==
             exception_handlers_.end());

  associated_info->exception_handler_key = handler.handle();
  exception_handlers_[handler.handle()] = std::move(handler);
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

  AddSignalHandler(watch_id, info.fd_handle, signals, &info);
  watches_[watch_id] = info;
  return WatchHandle(this, watch_id);
}

MessageLoop::WatchHandle MessageLoopAsync::WatchSocket(
    WatchMode mode, zx_handle_t socket_handle, SocketWatcher* watcher) {
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

  AddSignalHandler(watch_id, socket_handle, ZX_SOCKET_WRITABLE, &info);
  watches_[watch_id] = info;
  return WatchHandle(this, watch_id);
}

MessageLoop::WatchHandle MessageLoopAsync::WatchProcessExceptions(
    zx_handle_t process_handle, zx_koid_t process_koid,
    ZirconExceptionWatcher* watcher) {
  WatchInfo info;
  info.type = WatchType::kProcessExceptions;
  info.exception_watcher = watcher;
  info.task_koid = process_koid;
  info.task_handle = process_handle;

  int watch_id;
  {
    std::lock_guard<std::mutex> guard(mutex_);

    watch_id = next_watch_id_;
    next_watch_id_++;
  }

  // Watch all exceptions for the process.
  AddExceptionHandler(watch_id, process_handle, ZX_EXCEPTION_PORT_DEBUGGER,
                      &info);

  // Watch for the process terminated signal.
  AddSignalHandler(watch_id, process_handle, ZX_PROCESS_TERMINATED, &info);

  watches_[watch_id] = info;
  return WatchHandle(this, watch_id);
}

MessageLoop::WatchHandle MessageLoopAsync::WatchJobExceptions(
    zx_handle_t job_handle, zx_koid_t job_koid,
    ZirconExceptionWatcher* watcher) {
  WatchInfo info;
  info.type = WatchType::kJobExceptions;
  info.exception_watcher = watcher;
  info.task_koid = job_koid;
  info.task_handle = job_handle;

  int watch_id;
  {
    std::lock_guard<std::mutex> guard(mutex_);

    watch_id = next_watch_id_;
    next_watch_id_++;
  }

  // Create and track the exception handle.
  AddExceptionHandler(watch_id, job_handle, ZX_EXCEPTION_PORT_DEBUGGER, &info);
  watches_[watch_id] = info;
  return WatchHandle(this, watch_id);
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

void MessageLoopAsync::RunUntilTimeout(zx::duration timeout) {
  // Init should have been called.
  FXL_DCHECK(Current() == this);
  zx_status_t status;
  status = loop_.ResetQuit();
  FXL_DCHECK(status != ZX_ERR_BAD_STATE);
  status = loop_.Run(zx::deadline_after(timeout));
  FXL_DCHECK(status == ZX_OK || status == ZX_ERR_TIMED_OUT)
      << "Expected ZX_OK || ZX_ERR_TIMED_OUT, got " << ZxStatusToString(status);
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
  FXL_LOG(INFO) << __FUNCTION__;
  // Init should have been called.
  FXL_DCHECK(Current() == this);
  zx_status_t status;

  status = loop_.ResetQuit();
  FXL_DCHECK(status != ZX_ERR_BAD_STATE);
  status = loop_.Run();
  FXL_DCHECK(status == ZX_OK || status == ZX_ERR_CANCELED)
      << "Expected ZX_OK || ZX_ERR_CANCELED, got " << ZxStatusToString(status);
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
  TIME_BLOCK();

  uint32_t events = 0;
  fdio_unsafe_wait_end(info.fdio, observed, &events);

  if ((events & POLLERR) || (events & POLLHUP) || (events & POLLNVAL) ||
      (events & POLLRDHUP)) {
    info.fd_watcher->OnFDError(info.fd);

    // Don't dispatch any other notifications when there's an error. Zircon
    // seems to set readable and writable on error even if there's nothing
    // there.
    return;
  }

  // Since notifications can cause the watcher to be removed, this flag tracks
  // if anything has been issued and therefore we should re-check the watcher
  // registration before dereferencing anything.
  bool sent_notification = false;

  if (events & POLLIN) {
    info.fd_watcher->OnFDReadable(info.fd);
    sent_notification = true;
  }

  if (events & POLLOUT) {
    if (sent_notification) {
      std::lock_guard<std::mutex> guard(mutex_);
      if (watches_.find(watch_id) == watches_.end())
        return;
    }
    info.fd_watcher->OnFDWritable(info.fd);
    sent_notification = true;
  }
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
  TIME_BLOCK();

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
  TIME_BLOCK();

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
  TIME_BLOCK();

  FXL_LOG(INFO) << __FUNCTION__;
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
