// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/message_loop_target.h"

#include <lib/fdio/io.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
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

namespace debug_ipc {

namespace {

thread_local MessageLoopTarget* current_message_loop = nullptr;

}  // namespace

// MessageLoopTarget -----------------------------------------------------------

MessageLoopTarget::MessageLoopTarget() : loop_(&kAsyncLoopConfigAttachToCurrentThread) {}

MessageLoopTarget::~MessageLoopTarget() {
  FX_DCHECK(Current() != this);  // Cleanup should have been called.
}

bool MessageLoopTarget::Init(std::string* error_message) {
  FX_DCHECK(error_message);  // Error message out param not optional.
  if (!MessageLoop::Init(error_message))
    return false;

  FX_DCHECK(!current_message_loop);
  current_message_loop = this;

  zx::event::create(0, &task_event_);

  WatchInfo info;
  info.type = WatchType::kTask;
  zx_status_t status = AddSignalHandler(kTaskSignalKey, task_event_.get(), kTaskSignal, &info);

  if (status != ZX_OK) {
    *error_message = "Could not initialize message loop: ";
    error_message->append(debug_ipc::ZxStatusToString(status));
    return false;
  }

  watches_[kTaskSignalKey] = std::move(info);
  return true;
}

void MessageLoopTarget::Cleanup() {
  DEBUG_LOG(MessageLoop) << "Cleaning up the message loop.";

  // We need to remove the signal/exception handlers before the message loop
  // goes away.
  signal_handlers_.clear();
  channel_exception_handlers_.clear();

  FX_DCHECK(current_message_loop == this);
  current_message_loop = nullptr;

  MessageLoop::Cleanup();
}

// static
MessageLoopTarget* MessageLoopTarget::Current() { return current_message_loop; }

const MessageLoopTarget::WatchInfo* MessageLoopTarget::FindWatchInfo(int id) const {
  auto it = watches_.find(id);
  if (it == watches_.end())
    return nullptr;
  return &it->second;
}

zx_status_t MessageLoopTarget::AddSignalHandler(int id, zx_handle_t object, zx_signals_t signals,
                                                WatchInfo* associated_info) {
  SignalHandler handler;
  zx_status_t status = handler.Init(id, object, signals);
  if (status != ZX_OK)
    return status;

  // The handler should not be there already.
  FX_DCHECK(signal_handlers_.find(handler.handle()) == signal_handlers_.end());

  associated_info->signal_handler_key = handler.handle();
  signal_handlers_[handler.handle()] = std::move(handler);

  return ZX_OK;
}

zx_status_t MessageLoopTarget::AddChannelExceptionHandler(int id, zx_handle_t object,
                                                          uint32_t options, WatchInfo* info) {
  ChannelExceptionHandler handler;
  zx_status_t status = handler.Init(id, object, options);
  if (status != ZX_OK)
    return status;

  // The handler should not be there already.
  FX_DCHECK(channel_exception_handlers_.find(handler.handle()) ==
            channel_exception_handlers_.end());

  info->exception_channel_handler_key = handler.handle();
  channel_exception_handlers_[handler.handle()] = std::move(handler);

  return ZX_OK;
}

MessageLoop::WatchHandle MessageLoopTarget::WatchFD(WatchMode mode, int fd, FDWatcher* watcher) {
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

  zx_status_t status = AddSignalHandler(watch_id, info.fd_handle, signals, &info);
  if (status != ZX_OK)
    return WatchHandle();

  watches_[watch_id] = info;
  return WatchHandle(this, watch_id);
}

zx_status_t MessageLoopTarget::WatchSocket(WatchMode mode, zx_handle_t socket_handle,
                                           SocketWatcher* watcher, MessageLoop::WatchHandle* out) {
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

  zx_signals_t signals = ZX_SOCKET_PEER_CLOSED;
  if (mode == WatchMode::kRead || mode == WatchMode::kReadWrite)
    signals |= ZX_SOCKET_READABLE;

  if (mode == WatchMode::kWrite || mode == WatchMode::kReadWrite)
    signals |= ZX_SOCKET_WRITABLE;

  zx_status_t status = AddSignalHandler(watch_id, socket_handle, signals, &info);
  if (status != ZX_OK)
    return status;

  watches_[watch_id] = info;
  *out = WatchHandle(this, watch_id);
  return ZX_OK;
}

zx_status_t MessageLoopTarget::WatchProcessExceptions(WatchProcessConfig config,
                                                      MessageLoop::WatchHandle* out) {
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
  status = AddChannelExceptionHandler(watch_id, config.process_handle,
                                      ZX_EXCEPTION_CHANNEL_DEBUGGER, &info);
  if (status != ZX_OK)
    return status;

  // Watch for the process terminated signal.
  status = AddSignalHandler(watch_id, config.process_handle, ZX_PROCESS_TERMINATED, &info);
  if (status != ZX_OK)
    return status;

  DEBUG_LOG(MessageLoop) << "Watching process " << info.resource_name;

  watches_[watch_id] = info;
  *out = WatchHandle(this, watch_id);
  return ZX_OK;
}

zx_status_t MessageLoopTarget::WatchJobExceptions(WatchJobConfig config,
                                                  MessageLoop::WatchHandle* out) {
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
  zx_status_t status =
      AddChannelExceptionHandler(watch_id, config.job_handle, ZX_EXCEPTION_CHANNEL_DEBUGGER, &info);
  if (status != ZX_OK)
    return status;

  DEBUG_LOG(MessageLoop) << "Watching job " << info.resource_name;

  watches_[watch_id] = info;
  *out = WatchHandle(this, watch_id);
  return ZX_OK;
}

bool MessageLoopTarget::CheckAndProcessPendingTasks() {
  std::lock_guard<std::mutex> guard(mutex_);

  // We clear the event, otherwise it will trigger again and again
  task_event_.signal(kTaskSignal, 0);

  // Do a C++ task.
  if (ProcessPendingTask()) {
    SetHasTasks();  // Enqueue another task signal.
    return true;
  }
  return false;
}

void MessageLoopTarget::HandleChannelException(const ChannelExceptionHandler& handler,
                                               zx::exception exception,
                                               zx_exception_info_t exception_info) {
  WatchInfo* watch_info = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = watches_.find(handler.watch_info_id());
    FX_DCHECK(it != watches_.end());
    watch_info = &it->second;
  }

  if (watch_info->type != WatchType::kProcessExceptions &&
      watch_info->type != WatchType::kJobExceptions) {
    FX_NOTREACHED() << "Should only receive exceptions.";
    return;
  }

  FX_DCHECK(watch_info->exception_watcher);

  // We should only receive exceptions here.
  switch (watch_info->type) {
    case WatchType::kTask:
    case WatchType::kFdio:
    case WatchType::kSocket:
      FX_NOTREACHED() << "Should only receive exceptions.";
      return;
    case WatchType::kProcessExceptions:
      OnProcessException(*watch_info, std::move(exception), exception_info);
      return;
    case WatchType::kJobExceptions:
      OnJobException(*watch_info, std::move(exception), exception_info);
      return;
  }

  FX_NOTREACHED();
}

uint64_t MessageLoopTarget::GetMonotonicNowNS() const {
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
void MessageLoopTarget::RunImpl() {
  // Init should have been called.
  FX_DCHECK(Current() == this);
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
    FX_DCHECK(status != ZX_ERR_BAD_STATE);
    status = loop_.Run(time);
    FX_DCHECK(status == ZX_OK || status == ZX_ERR_CANCELED || status == ZX_ERR_TIMED_OUT)
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

void MessageLoopTarget::QuitNow() {
  MessageLoop::QuitNow();
  loop_.Quit();
}

void MessageLoopTarget::StopWatching(int id) {
  // The dispatch code for watch callbacks requires this be called on the
  // same thread as the message loop is.
  FX_DCHECK(Current() == this);

  std::lock_guard<std::mutex> guard(mutex_);

  auto found = watches_.find(id);
  FX_DCHECK(found != watches_.end());

  WatchInfo& info = found->second;
  // BufferedFD constantly creates and destroys FD handles, flooding the log
  // with non-helpful logging statements.
  if (info.type != WatchType::kFdio) {
    DEBUG_LOG(MessageLoop) << "Stop watching " << WatchTypeToString(info.type) << " "
                           << info.resource_name;
  }

  switch (info.type) {
    case WatchType::kProcessExceptions: {
      RemoveChannelExceptionHandler(&info);
      RemoveSignalHandler(&info);
      break;
    }
    case WatchType::kJobExceptions: {
      RemoveChannelExceptionHandler(&info);
      break;
    }
    case WatchType::kFdio:
      fdio_unsafe_release(info.fdio);
      // fallthrough
    case WatchType::kTask:
    case WatchType::kSocket:
      RemoveSignalHandler(&info);
      break;
  }
  watches_.erase(found);
}

void MessageLoopTarget::SetHasTasks() { task_event_.signal(0, kTaskSignal); }

void MessageLoopTarget::OnFdioSignal(int watch_id, const WatchInfo& info, zx_signals_t observed) {
  uint32_t events = 0;
  fdio_unsafe_wait_end(info.fdio, observed, &events);

  if ((events & POLLERR) || (events & POLLHUP) || (events & POLLNVAL) || (events & POLLRDHUP)) {
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

void MessageLoopTarget::RemoveSignalHandler(WatchInfo* info) {
  const async_wait_t* key = info->signal_handler_key;
  FX_DCHECK(key);

  size_t erase_count = signal_handlers_.erase(key);
  FX_DCHECK(erase_count == 1u);

  info->signal_handler_key = nullptr;
}

void MessageLoopTarget::RemoveChannelExceptionHandler(WatchInfo* info) {
  const async_wait_t* key = info->exception_channel_handler_key;
  FX_DCHECK(key);

  size_t erase_count = channel_exception_handlers_.erase(key);
  FX_DCHECK(erase_count == 1u);

  info->exception_channel_handler_key = nullptr;
}

void MessageLoopTarget::OnProcessException(const WatchInfo& info, zx::exception exception,
                                           zx_exception_info_t exception_info) {
  switch (exception_info.type) {
    case ZX_EXCP_THREAD_STARTING:
      info.exception_watcher->OnThreadStarting(std::move(exception), exception_info);
      break;
    case ZX_EXCP_THREAD_EXITING:
      info.exception_watcher->OnThreadExiting(std::move(exception), exception_info);
      break;
    case ZX_EXCP_GENERAL:
    case ZX_EXCP_FATAL_PAGE_FAULT:
    case ZX_EXCP_UNDEFINED_INSTRUCTION:
    case ZX_EXCP_SW_BREAKPOINT:
    case ZX_EXCP_HW_BREAKPOINT:
    case ZX_EXCP_UNALIGNED_ACCESS:
    case ZX_EXCP_POLICY_ERROR:
      info.exception_watcher->OnException(std::move(exception), exception_info);
      break;
    default:
      FX_NOTREACHED();
  }
}

void MessageLoopTarget::OnProcessTerminated(const WatchInfo& info, zx_signals_t observed) {
  FX_DCHECK(observed & ZX_PROCESS_TERMINATED);
  info.exception_watcher->OnProcessTerminated(info.task_koid);
}

void MessageLoopTarget::OnJobException(const WatchInfo& info, zx::exception exception,
                                       zx_exception_info_t exception_info) {
  // Currently job exceptions only track process starting exceptions.
  // TODO(fxbug.dev/34167): Debugger job exception ports should receive all exceptions.
  if (exception_info.type != ZX_EXCP_PROCESS_STARTING) {
    FX_NOTREACHED();
    return;
  }

  info.exception_watcher->OnProcessStarting(std::move(exception), exception_info);
}

void MessageLoopTarget::OnSocketSignal(int watch_id, const WatchInfo& info, zx_signals_t observed) {
  if (observed & ZX_SOCKET_PEER_CLOSED) {
    info.socket_watcher->OnSocketError(info.socket_handle);
    return;
  }

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

const char* WatchTypeToString(WatchType type) {
  switch (type) {
    case WatchType::kFdio:
      return "FDIO";
    case WatchType::kJobExceptions:
      return "Job";
    case WatchType::kProcessExceptions:
      return "Process";
    case WatchType::kTask:
      return "Task";
    case WatchType::kSocket:
      return "Socket";
  }

  FX_NOTREACHED();
  return "";
}

}  // namespace debug_ipc
