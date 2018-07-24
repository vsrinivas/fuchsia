// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/helper/message_loop_zircon.h"

#include <lib/fdio/io.h>
#include <lib/fdio/private.h>
#include <lib/zx/handle.h>
#include <lib/zx/process.h>
#include <zircon/syscalls/exception.h>

#include "garnet/lib/debug_ipc/helper/fd_watcher.h"
#include "garnet/lib/debug_ipc/helper/socket_watcher.h"
#include "garnet/lib/debug_ipc/helper/zircon_exception_watcher.h"
#include "garnet/public/lib/fxl/logging.h"

namespace debug_ipc {

namespace {

// This signal on the task_event_ indicates there is work to do.
constexpr uint32_t kTaskSignal = ZX_USER_SIGNAL_0;

// 0 is an invalid ID for watchers, so is safe to use here.
constexpr uint64_t kTaskSignalKey = 0;

thread_local MessageLoopZircon* current_message_loop_zircon = nullptr;

}  // namespace

// Everything in this class must be simple and copyable since we copy this
// structure for every call (to avoid locking problems).
struct MessageLoopZircon::WatchInfo {
  WatchType type = WatchType::kFdio;

  // FDIO-specific watcher parameters.
  int fd = -1;
  fdio_t* fdio = nullptr;
  FDWatcher* fd_watcher = nullptr;
  zx_handle_t fd_handle = ZX_HANDLE_INVALID;

  // Socket-specific parameters.
  SocketWatcher* socket_watcher = nullptr;
  zx_handle_t socket_handle = ZX_HANDLE_INVALID;

  // Process-exception-specific parameters.
  ZirconExceptionWatcher* exception_watcher = nullptr;
  zx_koid_t process_koid = 0;
  zx_handle_t process_handle = ZX_HANDLE_INVALID;
};

MessageLoopZircon::MessageLoopZircon() {
  zx::port::create(0, &port_);

  zx::event::create(0, &task_event_);
  task_event_.wait_async(port_, kTaskSignalKey, kTaskSignal,
                         ZX_WAIT_ASYNC_REPEATING);
}

MessageLoopZircon::~MessageLoopZircon() {
  FXL_DCHECK(Current() != this);  // Cleanup should have been called.
}

void MessageLoopZircon::Init() {
  MessageLoop::Init();

  FXL_DCHECK(!current_message_loop_zircon);
  current_message_loop_zircon = this;
}

void MessageLoopZircon::Cleanup() {
  FXL_DCHECK(current_message_loop_zircon == this);
  current_message_loop_zircon = nullptr;

  MessageLoop::Cleanup();
}

// static
MessageLoopZircon* MessageLoopZircon::Current() {
  return current_message_loop_zircon;
}

MessageLoop::WatchHandle MessageLoopZircon::WatchFD(WatchMode mode, int fd,
                                                    FDWatcher* watcher) {
  WatchInfo info;
  info.type = WatchType::kFdio;
  info.fd_watcher = watcher;
  info.fd = fd;
  info.fdio = __fdio_fd_to_io(fd);
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
  __fdio_wait_begin(info.fdio, events, &info.fd_handle, &signals);
  if (info.fd_handle == ZX_HANDLE_INVALID)
    return WatchHandle();

  int watch_id;
  {
    std::lock_guard<std::mutex> guard(mutex_);

    watch_id = next_watch_id_;
    next_watch_id_++;
    if (zx_object_wait_async(info.fd_handle, port_.get(),
                             static_cast<uint64_t>(watch_id), signals,
                             ZX_WAIT_ASYNC_REPEATING) != ZX_OK)
      return WatchHandle();

    watches_[watch_id] = info;
  }
  return WatchHandle(this, watch_id);
}

MessageLoop::WatchHandle MessageLoopZircon::WatchSocket(
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

    if (mode == WatchMode::kRead || mode == WatchMode::kReadWrite) {
      zx_status_t status =
          zx_object_wait_async(socket_handle, port_.get(), watch_id,
                               ZX_SOCKET_READABLE, ZX_WAIT_ASYNC_REPEATING);
      if (status != ZX_OK)
        return WatchHandle();
    }

    if (mode == WatchMode::kWrite || mode == WatchMode::kReadWrite) {
      zx_status_t status =
          zx_object_wait_async(socket_handle, port_.get(), watch_id,
                               ZX_SOCKET_WRITABLE, ZX_WAIT_ASYNC_REPEATING);
      if (status != ZX_OK)
        return WatchHandle();
    }

    watches_[watch_id] = info;
  }
  return WatchHandle(this, watch_id);
}

MessageLoop::WatchHandle MessageLoopZircon::WatchProcessExceptions(
    zx_handle_t process_handle, zx_koid_t process_koid,
    ZirconExceptionWatcher* watcher) {
  WatchInfo info;
  info.type = WatchType::kProcessExceptions;
  info.exception_watcher = watcher;
  info.process_koid = process_koid;
  info.process_handle = process_handle;

  int watch_id;
  {
    std::lock_guard<std::mutex> guard(mutex_);

    watch_id = next_watch_id_;
    next_watch_id_++;

    // Bind to the exception port.
    zx_status_t status = zx_task_bind_exception_port(
        process_handle, port_.get(), watch_id, ZX_EXCEPTION_PORT_DEBUGGER);
    if (status != ZX_OK)
      return WatchHandle();

    // Also watch for process termination.
    status =
        zx_object_wait_async(process_handle, port_.get(), watch_id,
                             ZX_PROCESS_TERMINATED, ZX_WAIT_ASYNC_REPEATING);
    if (status != ZX_OK)
      return WatchHandle();

    watches_[watch_id] = info;
  }
  return WatchHandle(this, watch_id);
}

zx_status_t MessageLoopZircon::ResumeFromException(zx_handle_t thread_handle,
                                                   uint32_t options) {
  return zx_task_resume_from_exception(thread_handle, port_.get(), options);
}

void MessageLoopZircon::RunImpl() {
  // Init should have been called.
  FXL_DCHECK(Current() == this);

  zx_port_packet_t packet;
  while (!should_quit() && port_.wait(zx::time::infinite(), &packet) == ZX_OK) {
    WatchInfo* watch_info = nullptr;
    {
      std::lock_guard<std::mutex> guard(mutex_);
      if (packet.key == kTaskSignalKey) {
        // Do a C++ task.
        if (ProcessPendingTask())
          SetHasTasks();  // Enqueue another task signal.
        continue;
      }

      // Some event being watched.
      auto found = watches_.find(packet.key);
      if (found == watches_.end()) {
        FXL_NOTREACHED();
        continue;
      }
      watch_info = &found->second;
    }

    // Dispatch the watch callback outside of the lock. This depends on the
    // stability of the WatchInfo pointer in the map (std::map is stable across
    // updates) and the watch not getting unregistered from another thread
    // asynchronously (which the API requires and is enforced by a DCHECK in
    // the StopWatching impl).
    switch (watch_info->type) {
      case WatchType::kFdio:
        OnFdioSignal(packet.key, *watch_info, packet);
        break;
      case WatchType::kProcessExceptions:
        OnProcessException(*watch_info, packet);
        break;
      case WatchType::kSocket:
        OnSocketSignal(packet.key, *watch_info, packet);
        break;
      default:
        FXL_NOTREACHED();
    }
  }
}

void MessageLoopZircon::StopWatching(int id) {
  // The dispatch code for watch callbacks requires this be called on the
  // same thread as the message loop is.
  FXL_DCHECK(Current() == this);

  std::lock_guard<std::mutex> guard(mutex_);

  auto found = watches_.find(id);
  if (found == watches_.end()) {
    FXL_NOTREACHED();
    return;
  }
  WatchInfo& info = found->second;

  switch (info.type) {
    case WatchType::kFdio:
      port_.cancel(*zx::unowned_handle(info.fd_handle),
                   static_cast<uint64_t>(id));
      break;
    case WatchType::kProcessExceptions: {
      zx::unowned_process process(info.process_handle);
      // Binding an invalid port will detach from the exception port.
      process->bind_exception_port(zx::port(), 0, ZX_EXCEPTION_PORT_DEBUGGER);
      // Stop watching for process termination.
      port_.cancel(*process, id);
      break;
    }
    case WatchType::kSocket:
      port_.cancel(*zx::unowned_handle(info.socket_handle), id);
      break;
    default:
      FXL_NOTREACHED();
      break;
  }
  watches_.erase(found);
}

void MessageLoopZircon::SetHasTasks() { task_event_.signal(0, kTaskSignal); }

void MessageLoopZircon::OnFdioSignal(int watch_id, const WatchInfo& info,
                                     const zx_port_packet_t& packet) {
  uint32_t events = 0;
  __fdio_wait_end(info.fdio, packet.signal.observed, &events);

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
      if (watches_.find(packet.key) == watches_.end())
        return;
    }
    info.fd_watcher->OnFDWritable(info.fd);
    sent_notification = true;
  }
}

void MessageLoopZircon::OnProcessException(const WatchInfo& info,
                                           const zx_port_packet_t& packet) {
  if (ZX_PKT_IS_EXCEPTION(packet.type)) {
    // All debug exceptions.
    switch (packet.type) {
      case ZX_EXCP_THREAD_STARTING:
        info.exception_watcher->OnThreadStarting(info.process_koid,
                                                 packet.exception.tid);
        break;
      case ZX_EXCP_THREAD_EXITING:
        info.exception_watcher->OnThreadExiting(info.process_koid,
                                                packet.exception.tid);
        break;
      case ZX_EXCP_GENERAL:
      case ZX_EXCP_FATAL_PAGE_FAULT:
      case ZX_EXCP_UNDEFINED_INSTRUCTION:
      case ZX_EXCP_SW_BREAKPOINT:
      case ZX_EXCP_HW_BREAKPOINT:
      case ZX_EXCP_UNALIGNED_ACCESS:
      case ZX_EXCP_POLICY_ERROR:
        info.exception_watcher->OnException(info.process_koid,
                                            packet.exception.tid, packet.type);
        break;
      default:
        FXL_NOTREACHED();
    }
  } else if (ZX_PKT_IS_SIGNAL_REP(packet.type) &&
             packet.signal.observed & ZX_PROCESS_TERMINATED) {
    // This type of watcher also gets process terminated signals.
    info.exception_watcher->OnProcessTerminated(info.process_koid);
  } else {
    FXL_NOTREACHED();
  }
}

void MessageLoopZircon::OnSocketSignal(int watch_id, const WatchInfo& info,
                                       const zx_port_packet_t& packet) {
  if (!ZX_PKT_IS_SIGNAL_REP(packet.type))
    return;

  // Dispatch readable signal.
  if (packet.signal.observed & ZX_SOCKET_READABLE)
    info.socket_watcher->OnSocketReadable(info.socket_handle);

  // When signaling both readable and writable, make sure the readable handler
  // didn't remove the watch.
  if ((packet.signal.observed & ZX_SOCKET_READABLE) &&
      (packet.signal.observed & ZX_SOCKET_WRITABLE)) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (watches_.find(packet.key) == watches_.end())
      return;
  }

  // Dispatch writable signal.
  if (packet.signal.observed & ZX_SOCKET_WRITABLE)
    info.socket_watcher->OnSocketWritable(info.socket_handle);
}

}  // namespace debug_ipc
