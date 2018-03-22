// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/debugged_thread.h"

#include <memory>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>

#include "garnet/bin/debug_agent/debug_agent.h"
#include "garnet/bin/debug_agent/debugged_process.h"
#include "garnet/bin/debug_agent/process_info.h"
#include "garnet/lib/debug_ipc/agent_protocol.h"
#include "garnet/lib/debug_ipc/message_reader.h"
#include "garnet/lib/debug_ipc/message_writer.h"
#include "garnet/lib/debug_ipc/stream_buffer.h"

DebuggedThread::DebuggedThread(DebuggedProcess* process,
                               zx::thread thread,
                               zx_koid_t koid, bool starting)
    : debug_agent_(process->debug_agent()),
      process_(process),
      thread_(std::move(thread)),
      koid_(koid) {
  if (starting)
    thread_.resume(ZX_RESUME_EXCEPTION);
}

DebuggedThread::~DebuggedThread() {
}

void DebuggedThread::OnException(uint32_t type) {
  suspend_reason_ = SuspendReason::kException;
  // TODO(brettw) suspend other threads in the process and other debugged
  // processes as desired.

  debug_ipc::NotifyException notify;
  notify.process_koid = process_->koid();
  FillThreadRecord(thread_, &notify.thread);

  switch (type) {
    case ZX_EXCP_SW_BREAKPOINT:
      notify.type = debug_ipc::NotifyException::Type::kSoftware;
      break;
    case ZX_EXCP_HW_BREAKPOINT:
      notify.type = debug_ipc::NotifyException::Type::kHardware;
      break;
    default:
      notify.type = debug_ipc::NotifyException::Type::kGeneral;
      break;
  }

  zx_thread_state_general_regs regs;
  zx_thread_read_state(thread_.get(), ZX_THREAD_STATE_GENERAL_REGS, &regs,
                       sizeof(regs));
#if defined(__x86_64__)
  notify.ip = regs.rip;
  notify.sp = regs.rsp;
#elif defined(__aarch64__)
  notify.ip = regs.pc;
  notify.sp = regs.sp;
#else
  #error Unsupported architecture.
#endif

  // Send notification.
  debug_ipc::MessageWriter writer;
  debug_ipc::WriteNotifyException(notify, &writer);
  debug_agent_->stream().Write(writer.MessageComplete());

  // Keep the thread suspended for the client.
}

void DebuggedThread::Continue() {
  if (suspend_reason_ == SuspendReason::kException)
    thread_.resume(ZX_RESUME_EXCEPTION);
  else if (suspend_reason_ == SuspendReason::kOther)
    thread_.resume(0);
}
