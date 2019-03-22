// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/message_loop_zircon.h"

#include <lib/fdio/io.h>
#include <lib/fdio/unsafe.h>
#include <lib/zx/handle.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <zircon/syscalls/exception.h>

#include "garnet/public/lib/fxl/logging.h"
#include "src/developer/debug/shared/fd_watcher.h"
#include "src/developer/debug/shared/socket_watcher.h"
#include "src/developer/debug/shared/zircon_exception_watcher.h"

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
  // Mostly for debugging purposes.
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

void MessageLoopZircon::Init() { InitTarget(); }

zx_status_t MessageLoopZircon::InitTarget() {
  MessageLoop::Init();

  FXL_DCHECK(!current_message_loop_zircon);
  current_message_loop_zircon = this;
  MessageLoopTarget::current_message_loop_type =
      MessageLoopTarget::Type::kZircon;

  return ZX_OK;
}

void MessageLoopZircon::Cleanup() {
  FXL_DCHECK(current_message_loop_zircon == this);
  current_message_loop_zircon = nullptr;
  MessageLoopTarget::current_message_loop_type = MessageLoopTarget::Type::kLast;

  MessageLoop::Cleanup();
}

void MessageLoopZircon::QuitNow() { MessageLoop::QuitNow(); }

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
    if (zx_object_wait_async(info.fd_handle, port_.get(),
                             static_cast<uint64_t>(watch_id), signals,
                             ZX_WAIT_ASYNC_REPEATING) != ZX_OK)
      return WatchHandle();

    watches_[watch_id] = info;
  }

  return WatchHandle(this, watch_id);
}

zx_status_t MessageLoopZircon::WatchSocket(WatchMode mode,
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

    if (mode == WatchMode::kRead || mode == WatchMode::kReadWrite) {
      zx_status_t status =
          zx_object_wait_async(socket_handle, port_.get(), watch_id,
                               ZX_SOCKET_READABLE, ZX_WAIT_ASYNC_REPEATING);
      if (status != ZX_OK)
        return status;
    }

    if (mode == WatchMode::kWrite || mode == WatchMode::kReadWrite) {
      zx_status_t status =
          zx_object_wait_async(socket_handle, port_.get(), watch_id,
                               ZX_SOCKET_WRITABLE, ZX_WAIT_ASYNC_REPEATING);
      if (status != ZX_OK)
        return status;
    }

    watches_[watch_id] = info;
  }

  *out = WatchHandle(this, watch_id);
  return ZX_OK;
}

zx_status_t MessageLoopZircon::WatchProcessExceptions(
    WatchProcessConfig config, MessageLoop::WatchHandle* out) {
  WatchInfo info;
  info.type = WatchType::kProcessExceptions;
  info.resource_name = config.process_name;
  info.exception_watcher = config.watcher;
  info.task_koid = config.process_koid;
  info.task_handle = config.process_handle;

  int watch_id;
  {
    std::lock_guard<std::mutex> guard(mutex_);

    watch_id = next_watch_id_;
    next_watch_id_++;

    // Bind to the exception port.
    zx_status_t status =
        zx_task_bind_exception_port(config.process_handle, port_.get(),
                                    watch_id, ZX_EXCEPTION_PORT_DEBUGGER);
    if (status != ZX_OK)
      return status;

    // Also watch for process termination.
    status =
        zx_object_wait_async(config.process_handle, port_.get(), watch_id,
                             ZX_PROCESS_TERMINATED, ZX_WAIT_ASYNC_REPEATING);
    if (status != ZX_OK)
      return status;

    watches_[watch_id] = info;
  }

  *out = WatchHandle(this, watch_id);
  return ZX_OK;
}

zx_status_t MessageLoopZircon::WatchJobExceptions(
    WatchJobConfig config, MessageLoop::WatchHandle* out) {
  WatchInfo info;
  info.type = WatchType::kJobExceptions;
  info.resource_name = config.job_name;
  info.exception_watcher = config.watcher;
  info.task_koid = config.job_koid;
  info.task_handle = config.job_handle;

  int watch_id;
  {
    std::lock_guard<std::mutex> guard(mutex_);

    watch_id = next_watch_id_;
    next_watch_id_++;

    // Bind to the exception port.
    zx_status_t status = zx_task_bind_exception_port(
        config.job_handle, port_.get(), watch_id, ZX_EXCEPTION_PORT_DEBUGGER);
    if (status != ZX_OK)
      return status;

    watches_[watch_id] = info;
  }

  *out = WatchHandle(this, watch_id);
  return ZX_OK;
}

// |thread_koid| is unused in this message loop.
zx_status_t MessageLoopZircon::ResumeFromException(zx_koid_t,
                                                   zx::thread& thread,
                                                   uint32_t options) {
  return thread.resume_from_exception(port_, options);
}

bool MessageLoopZircon::CheckAndProcessPendingTasks() {
  std::lock_guard<std::mutex> guard(mutex_);
  // Do a C++ task.
  if (ProcessPendingTask()) {
    SetHasTasks();  // Enqueue another task signal.
    return true;
  }
  return false;
}

void MessageLoopZircon::HandleException(zx_port_packet_t packet) {
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
      // TODO(zX-2623): zircon is going to clean up stale packets from ports
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
    case WatchType::kFdio:
      OnFdioSignal(packet.key, *watch_info, packet);
      break;
    case WatchType::kProcessExceptions:
      OnProcessException(*watch_info, packet);
      break;
    case WatchType::kJobExceptions:
      OnJobException(*watch_info, packet);
      break;
    case WatchType::kSocket:
      OnSocketSignal(packet.key, *watch_info, packet);
      break;
    default:
      FXL_NOTREACHED();
  }
}

void MessageLoopZircon::RunUntilTimeout(zx::duration timeout) {
  // Init should have been called.
  FXL_DCHECK(Current() == this);
  zx_port_packet_t packet;
  zx_status_t status = port_.wait(zx::deadline_after(timeout), &packet);
  FXL_DCHECK(status == ZX_OK || status == ZX_ERR_TIMED_OUT);
  if (status == ZX_OK) {
    HandleException(packet);
  }
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
void MessageLoopZircon::RunImpl() {
  // Init should have been called.
  FXL_DCHECK(Current() == this);

  zx_port_packet_t packet;
  while (!should_quit() && port_.wait(zx::time::infinite(), &packet) == ZX_OK) {
    // We check first for pending C++ tasks. If an event was handled, it will
    // signal the associated zx::event in order to trigger the port once more
    // (this is the way we process an enqueued event). If there is no enqueued
    // event, we won't trigger the event and go back to wait on the port.
    if (packet.key == kTaskSignalKey) {
      CheckAndProcessPendingTasks();
      continue;
    }

    // If it wasn't a task, we check for what kind of exception it was and
    // handle it.
    HandleException(packet);

    // The exception handling could have added more pending work, so we have to
    // re-check in order to correctly signal for new work.
    CheckAndProcessPendingTasks();
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
      zx::unowned_process process(info.task_handle);

      // Binding an invalid port will detach from the exception port.
      process->bind_exception_port(zx::port(), 0, ZX_EXCEPTION_PORT_DEBUGGER);
      // Stop watching for process events.
      port_.cancel(*process, id);
      break;
    }
    case WatchType::kJobExceptions: {
      zx::unowned_job job(info.task_handle);
      // Binding an invalid port will detach from the exception port.
      job->bind_exception_port(zx::port(), 0, ZX_EXCEPTION_PORT_DEBUGGER);
      // Stop watching for job events.
      port_.cancel(*job, id);
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
  fdio_unsafe_wait_end(info.fdio, packet.signal.observed, &events);

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
        info.exception_watcher->OnThreadStarting(info.task_koid,
                                                 packet.exception.tid);
        break;
      case ZX_EXCP_THREAD_EXITING:
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
        info.exception_watcher->OnException(info.task_koid,
                                            packet.exception.tid, packet.type);
        break;
      default:
        FXL_NOTREACHED();
    }
  } else if (ZX_PKT_IS_SIGNAL_REP(packet.type) &&
             packet.signal.observed & ZX_PROCESS_TERMINATED) {
    // This type of watcher also gets process terminated signals.
    info.exception_watcher->OnProcessTerminated(info.task_koid);
  } else {
    FXL_NOTREACHED();
  }
}

void MessageLoopZircon::OnJobException(const WatchInfo& info,
                                       const zx_port_packet_t& packet) {
  if (ZX_PKT_IS_EXCEPTION(packet.type)) {
    // All debug exceptions.
    switch (packet.type) {
      case ZX_EXCP_PROCESS_STARTING:
        info.exception_watcher->OnProcessStarting(
            info.task_koid, packet.exception.pid, packet.exception.tid);
        break;
      default:
        FXL_NOTREACHED();
    }
  } else {
    FXL_NOTREACHED();
  }
}

void MessageLoopZircon::OnSocketSignal(int watch_id, const WatchInfo& info,
                                       const zx_port_packet_t& packet) {
  if (!ZX_PKT_IS_SIGNAL_REP(packet.type))
    return;

  auto observed = packet.signal.observed;

  // See if the socket was closed.
  if ((observed & ZX_SOCKET_PEER_CLOSED) ||
      (observed & ZX_SIGNAL_HANDLE_CLOSED)) {
    info.socket_watcher->OnSocketError(info.socket_handle);
    // |info| is can be deleted at this point, so don't use it anymore.
    return;
  }

  // Dispatch readable signal.
  if (observed & ZX_SOCKET_READABLE)
    info.socket_watcher->OnSocketReadable(info.socket_handle);

  // When signaling both readable and writable, make sure the readable handler
  // didn't remove the watch.
  if ((observed & ZX_SOCKET_READABLE) && (observed & ZX_SOCKET_WRITABLE)) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (watches_.find(packet.key) == watches_.end())
      return;
  }

  // Dispatch writable signal.
  if (observed & ZX_SOCKET_WRITABLE)
    info.socket_watcher->OnSocketWritable(info.socket_handle);
}

}  // namespace debug_ipc
