// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/exception_handler.h"

#include <stdio.h>
#include <utility>
#include <fbl/auto_lock.h>
#include <zircon/syscalls/exception.h>

#include "garnet/bin/debug_agent/object_util.h"
#include "garnet/public/lib/fxl/logging.h"

namespace {

// Key used for waiting on a port for the socket and quit events. Everything
// related to a debugged process uses that process' koid for the key, so this
// value is explicitly an invalid koid.
constexpr uint64_t kMetaKey = 0;

// This signal on the quit_event_ signals that the loop should exit.
constexpr uint32_t kQuitSignal = ZX_USER_SIGNAL_0;

}  // namespace

struct ExceptionHandler::WatchedProcess {
  zx_koid_t koid;
  zx_handle_t process;
};

ExceptionHandler::ExceptionHandler() {
  socket_buffer_.set_writer(this);
}

ExceptionHandler::~ExceptionHandler() {}

bool ExceptionHandler::Start(zx::socket socket) {
  zx_status_t status = zx::port::create(0, &port_);
  if (status != ZX_OK)
    return false;

  // Create and hook up the quit event.
  status = zx::event::create(0, &quit_event_);
  if (status != ZX_OK)
    return false;
  status = quit_event_.wait_async(port_, kMetaKey, kQuitSignal,
                                  ZX_WAIT_ASYNC_REPEATING);

  // Attach the socket for commands.
  socket_ = std::move(socket);
  status = socket_.wait_async(port_, kMetaKey, ZX_SOCKET_READABLE,
                              ZX_WAIT_ASYNC_REPEATING);
  if (status != ZX_OK)
    return false;
  status = socket_.wait_async(port_, kMetaKey, ZX_SOCKET_WRITABLE,
                              ZX_WAIT_ASYNC_REPEATING);
  if (status != ZX_OK)
    return false;

  thread_ = std::make_unique<std::thread>(&ExceptionHandler::DoThread, this);
  return true;
}

void ExceptionHandler::Shutdown() {
  // Signal the quit event, which is user signal 0 on the socket. This will
  // cause the background thread to wake up and terminate.
  quit_event_.signal(0, kQuitSignal);
  thread_->join();
}

bool ExceptionHandler::Attach(zx_koid_t koid, zx_handle_t process) {
  FXL_DCHECK(!WatchedProcessForKoid(koid));

  auto deb_proc = std::make_unique<WatchedProcess>();
  deb_proc->koid = koid;
  deb_proc->process = process;
  processes_.push_back(std::move(deb_proc));

  // Attach to the special debugger exception port.
  zx_status_t status = zx_task_bind_exception_port(
      process, port_.get(), koid, ZX_EXCEPTION_PORT_DEBUGGER);
  if (status != ZX_OK)
    return false;

  status = zx_object_wait_async(process, port_.get(), koid,
                                ZX_PROCESS_TERMINATED,
                                ZX_WAIT_ASYNC_REPEATING);
  if (status != ZX_OK)
    return false;

  return true;
}

void ExceptionHandler::Detach(zx_koid_t koid) {
  for (size_t i = 0; i < processes_.size(); i++) {
    WatchedProcess* proc = processes_[i].get();
    if (proc->koid == koid) {
      // Binding an invalid port will detach from the exception port.
      zx_task_bind_exception_port(proc->process, ZX_HANDLE_INVALID, koid,
                                  ZX_EXCEPTION_PORT_DEBUGGER);
      port_.cancel(proc->process, koid);
      processes_.erase(processes_.begin() + i);
      return;
    }
  }
  FXL_NOTREACHED();  // Should have found the process above.
}

size_t ExceptionHandler::ConsumeStreamBufferData(const char* data, size_t len) {
  size_t written = 0;
  socket_.write(0, data, len, &written);
  return written;
}

void ExceptionHandler::DoThread() {
  zx_port_packet_t packet;
  while (port_.wait(zx::time::infinite(), &packet, 1) == ZX_OK) {
    if (ZX_PKT_IS_EXCEPTION(packet.type)) {
      const WatchedProcess* proc = WatchedProcessForKoid(packet.exception.pid);
      if (!proc) {
        fprintf(stderr, "Got exception for a process we're not debugging.\n");
        return;
      }
      zx::thread thread = ThreadForKoid(proc->process, packet.exception.tid);

      switch (packet.type) {
        case ZX_EXCP_THREAD_STARTING:
          process_watcher_->OnThreadStarting(std::move(thread), proc->koid,
                                             packet.exception.tid);
          break;
        case ZX_EXCP_THREAD_EXITING:
          process_watcher_->OnThreadExiting(proc->koid, packet.exception.tid);
          break;
        case ZX_EXCP_GENERAL:
        case ZX_EXCP_FATAL_PAGE_FAULT:
        case ZX_EXCP_UNDEFINED_INSTRUCTION:
        case ZX_EXCP_SW_BREAKPOINT:
        case ZX_EXCP_HW_BREAKPOINT:
        case ZX_EXCP_UNALIGNED_ACCESS:
        case ZX_EXCP_POLICY_ERROR:
          process_watcher_->OnException(proc->koid, packet.exception.tid,
                                        packet.type);
          break;
        default:
          fprintf(stderr, "Unknown exception.\n");
      }
    } else if (ZX_PKT_IS_SIGNAL_REP(packet.type) && packet.key == kMetaKey &&
               packet.signal.observed & ZX_SOCKET_READABLE) {
      OnSocketReadable();
    } else if (ZX_PKT_IS_SIGNAL_REP(packet.type) && packet.key == kMetaKey &&
               packet.signal.observed & ZX_SOCKET_WRITABLE) {
      // Note: this will reenter us and call ConsumeStreamBufferData().
      socket_buffer_.SetWritable();
    } else if (ZX_PKT_IS_SIGNAL_REP(packet.type) &&
               packet.signal.observed & ZX_PROCESS_TERMINATED) {
      // Note: this will reenter us and call Detach for this process.
      process_watcher_->OnProcessTerminated(packet.key);
    } else if (ZX_PKT_IS_SIGNAL_REP(packet.type) && packet.key == kMetaKey &&
               packet.signal.observed & kQuitSignal) {
      // Quit event
      return;
    } else {
      fprintf(stderr, "Unknown signal.\n");
    }
  }
  return;
}

void ExceptionHandler::OnSocketReadable() {
  // Messages from the client to the agent are typically small so we don't need
  // a very large buffer.
  constexpr size_t kBufSize = 1024;

  // Add all available data to the socket buffer.
  while (true) {
    std::vector<char> buffer;
    buffer.resize(kBufSize);

    size_t num_read = 0;
    if (socket_.read(0, &buffer[0], kBufSize, &num_read) == ZX_OK) {
      buffer.resize(num_read);
      socket_buffer_.AddReadData(std::move(buffer));
    } else {
      break;
    }
    // TODO(brettw) it would be nice to yield here after reading "a bunch" of
    // data so this pipe doesn't starve the entire app.
  }

  read_watcher_->OnHandleReadable();
}

const ExceptionHandler::WatchedProcess* ExceptionHandler::WatchedProcessForKoid(
    zx_koid_t koid) {
  for (const auto& proc : processes_) {
    if (proc->koid == koid)
      return proc.get();
  }
  return nullptr;
}
