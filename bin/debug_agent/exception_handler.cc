// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/exception_handler.h"

#include <stdio.h>
#include <utility>

#include <fbl/auto_lock.h>
#include <zircon/syscalls/exception.h>

namespace {

// Key used for waiting on a port for the socket and quit events. Everything
// related to a debugged process uses that process' koid for the key, so this
// value is explicitly an invalid koid.
constexpr uint64_t kMetaKey = 0;

// This signal on the quit_event_ signals that the loop should exit.
constexpr uint32_t kQuitSignal = ZX_USER_SIGNAL_0;

zx::thread ThreadForKoid(const zx::process& process, uint64_t thread_koid) {
  zx_handle_t thread_handle = ZX_HANDLE_INVALID;
  zx_object_get_child(process.get(), thread_koid, ZX_RIGHT_SAME_RIGHTS, &thread_handle);
  return zx::thread(thread_handle);
}

zx_koid_t KoidForProcess(const zx::process& process) {
  zx_info_handle_basic_t info;
  if (process.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr,
                       nullptr) != ZX_OK)
    return 0;
  return info.koid;
}

}  // namespace

struct ExceptionHandler::DebuggedProcess {
  zx_koid_t koid;
  zx::process process;
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
  set_sink(nullptr);
}

bool ExceptionHandler::Attach(zx::process&& in_process) {
  auto owned_deb_proc = std::make_unique<DebuggedProcess>();
  DebuggedProcess* deb_proc = owned_deb_proc.get();  // Used after move below.
  deb_proc->koid = KoidForProcess(in_process);
  deb_proc->process = std::move(in_process);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    processes_.push_back(std::move(owned_deb_proc));
  }

  zx_koid_t koid = KoidForProcess(deb_proc->process);

  // Attach to the special debugger exception port.
  zx_status_t status = deb_proc->process.bind_exception_port(
      port_, koid, ZX_EXCEPTION_PORT_DEBUGGER);
  if (status != ZX_OK)
    return false;

  status = deb_proc->process.wait_async(port_, koid, ZX_PROCESS_TERMINATED,
                                        ZX_WAIT_ASYNC_REPEATING);
  if (status != ZX_OK)
    return false;

  return true;
}

size_t ExceptionHandler::ConsumeStreamBufferData(const char* data, size_t len) {
  size_t written = 0;
  socket_.write(0, data, len, &written);
  return written;
}

void ExceptionHandler::DoThread() {
  zx_port_packet_t packet;
  while (port_.wait(zx::time::infinite(), &packet, 0) == ZX_OK) {
    if (ZX_PKT_IS_EXCEPTION(packet.type)) {
      const DebuggedProcess* proc = ProcessForKoid(packet.exception.pid);
      if (!proc) {
        fprintf(stderr, "Got exception for a process we're not debugging.\n");
        return;
      }
      zx::thread thread = ThreadForKoid(proc->process, packet.exception.tid);

      switch (packet.type) {
        case ZX_EXCP_GENERAL:
          OnGeneralException(packet, thread);
          break;
        case ZX_EXCP_FATAL_PAGE_FAULT:
          OnFatalPageFault(packet, thread);
          break;
        case ZX_EXCP_UNDEFINED_INSTRUCTION:
          OnUndefinedInstruction(packet, thread);
          break;
        case ZX_EXCP_SW_BREAKPOINT:
          OnSoftwareBreakpoint(packet, thread);
          break;
        case ZX_EXCP_HW_BREAKPOINT:
          OnHardwareBreakpoint(packet, thread);
          break;
        case ZX_EXCP_UNALIGNED_ACCESS:
          OnUnalignedAccess(packet, thread);
          break;
        case ZX_EXCP_THREAD_STARTING:
          sink_->OnThreadStarting(thread);
          break;
        case ZX_EXCP_THREAD_EXITING:
          sink_->OnThreadExiting(thread);
          break;
        case ZX_EXCP_POLICY_ERROR:
          OnThreadPolicyError(packet, thread);
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
      OnProcessTerminated(packet);
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
  size_t available = 0;
  zx_status_t status = socket_.read(0, nullptr, 0, &available);
  if (status != ZX_OK)
    return;

  std::vector<char> buffer(available);
  socket_.read(0, &buffer[0], available, &available);
  socket_buffer_.AddReadData(std::move(buffer));

  sink_->OnStreamData();
}

void ExceptionHandler::OnProcessTerminated(const zx_port_packet_t& packet) {
  fprintf(stderr, "Process %llu terminated.\n", (unsigned long long)packet.key);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < processes_.size(); i++) {
      if (processes_[i]->koid == packet.key) {
        processes_.erase(processes_.begin() + i);
        return;
      }
    }
  }
  fprintf(stderr, "Got terminated for a process we're not watching.\n");
}

void ExceptionHandler::OnGeneralException(const zx_port_packet_t& packet,
                                          const zx::thread& thread) {
  fprintf(stderr, "Exception: general.\n");
}

void ExceptionHandler::OnFatalPageFault(const zx_port_packet_t& packet,
                                        const zx::thread& thread) {
  fprintf(stderr, "Exception: page fault.\n");
}

void ExceptionHandler::OnUndefinedInstruction(const zx_port_packet_t& packet,
                                              const zx::thread& thread) {
  fprintf(stderr, "Exception: undefined instruction.\n");
}

void ExceptionHandler::OnSoftwareBreakpoint(const zx_port_packet_t& packet,
                                            const zx::thread& thread) {
  fprintf(stderr, "Exception: software breakpoint.\n");
}

void ExceptionHandler::OnHardwareBreakpoint(const zx_port_packet_t& packet,
                                            const zx::thread& thread) {
  fprintf(stderr, "Exception: hardware breakpoint.\n");
}

void ExceptionHandler::OnUnalignedAccess(const zx_port_packet_t& packet,
                                         const zx::thread& thread) {
  fprintf(stderr, "Exception: unaligned access.\n");
}

void ExceptionHandler::OnThreadPolicyError(const zx_port_packet_t& packet,
                                           const zx::thread& thread) {
  fprintf(stderr, "Exception: thread policy error.\n");
}

const ExceptionHandler::DebuggedProcess* ExceptionHandler::ProcessForKoid(
    zx_koid_t koid) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& proc : processes_) {
    if (proc->koid == koid)
      return proc.get();
  }
  return nullptr;
}
