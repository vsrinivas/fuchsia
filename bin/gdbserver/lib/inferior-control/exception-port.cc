// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "exception-port.h"

#include <cinttypes>
#include <string>

#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

#include "lib/fxl/logging.h"
#include "lib/fsl/handles/object_info.h"
#include "lib/fsl/tasks/message_loop.h"

#include "garnet/lib/debugger_utils/util.h"

#include "process.h"

using std::lock_guard;
using std::mutex;

namespace debugserver {

namespace {

std::string IOPortPacketTypeToString(const zx_port_packet_t& pkt) {
  if (ZX_PKT_IS_EXCEPTION(pkt.type)) {
    return "ZX_PKT_TYPE_EXCEPTION";
  }
#define CASE_TO_STR(x) \
  case x:              \
    return #x
  switch (pkt.type) {
    CASE_TO_STR(ZX_PKT_TYPE_USER);
    CASE_TO_STR(ZX_PKT_TYPE_SIGNAL_ONE);
    CASE_TO_STR(ZX_PKT_TYPE_SIGNAL_REP);
    default:
      break;
  }
#undef CASE_TO_STR
  return "(unknown)";
}

}  // namespace

// static
ExceptionPort::Key ExceptionPort::g_key_counter = 0;

ExceptionPort::ExceptionPort() : keep_running_(false) {
  FXL_DCHECK(fsl::MessageLoop::GetCurrent());
  origin_task_runner_ = fsl::MessageLoop::GetCurrent()->task_runner();
}

ExceptionPort::~ExceptionPort() {
  if (eport_handle_)
    Quit();
}

bool ExceptionPort::Run() {
  FXL_DCHECK(!eport_handle_);
  FXL_DCHECK(!keep_running_);

  // Create an I/O port.
  zx_status_t status = zx::port::create(0, &eport_handle_);
  if (status < 0) {
    FXL_LOG(ERROR) << "Failed to create the exception port: "
                   << util::ZxErrorString(status);
    return false;
  }

  FXL_DCHECK(eport_handle_);

  keep_running_ = true;
  io_thread_ = std::thread(std::bind(&ExceptionPort::Worker, this));

  return true;
}

void ExceptionPort::Quit() {
  FXL_DCHECK(eport_handle_);
  FXL_DCHECK(keep_running_);

  FXL_LOG(INFO) << "Quitting exception port I/O loop";

  // Close the I/O port. This should cause zx_port_wait to return if one is
  // pending.
  keep_running_ = false;
  {
    lock_guard<mutex> lock(eport_mutex_);

    // The only way it seems possible to make the I/O thread return from
    // zx_port_wait is to queue a dummy packet on the port.
    zx_port_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = ZX_PKT_TYPE_USER;
    eport_handle_.queue(&packet, 0);
  }

  io_thread_.join();

  FXL_LOG(INFO) << "Exception port I/O loop exited";
}

ExceptionPort::Key ExceptionPort::Bind(zx_handle_t process_handle,
                                       const Callback& callback) {
  FXL_DCHECK(process_handle != ZX_HANDLE_INVALID);
  FXL_DCHECK(callback);
  FXL_DCHECK(eport_handle_);

  Key next_key = g_key_counter + 1;

  // Check for overflows. We don't keep track of which keys are ready to use and
  // which aren't. A 64-bit range is pretty big, so if we run out, we run out.
  if (!next_key) {
    FXL_LOG(ERROR) << "Ran out of keys!";
    return 0;
  }

  zx_status_t status =
      zx_task_bind_exception_port(process_handle, eport_handle_.get(),
                                    next_key, ZX_EXCEPTION_PORT_DEBUGGER);
  if (status < 0) {
    FXL_LOG(ERROR) << "Failed to bind exception port: "
                   << util::ZxErrorString(status);
    return 0;
  }

  // |next_key| should not have been used before.
  FXL_DCHECK(callbacks_.find(next_key) == callbacks_.end());

  callbacks_[next_key] = BindData(process_handle, callback);
  ++g_key_counter;

  FXL_VLOG(1) << "Exception port bound to process handle " << process_handle
              << " with key " << next_key;

  return next_key;
}

bool ExceptionPort::Unbind(const Key key) {
  const auto& iter = callbacks_.find(key);
  if (iter == callbacks_.end()) {
    FXL_VLOG(1) << "|key| not bound; Cannot unbind exception port";
    return false;
  }

  // Unbind the exception port. This is a best effort operation so if it fails,
  // there isn't really anything we can do to recover.
  zx_task_bind_exception_port(iter->second.process_handle, ZX_HANDLE_INVALID,
                                key, ZX_EXCEPTION_PORT_DEBUGGER);
  callbacks_.erase(iter);

  return true;
}

void ExceptionPort::Worker() {
  FXL_DCHECK(eport_handle_);

  // Give this thread an identifiable name for debugging purposes.
  fsl::SetCurrentThreadName("exception port reader");

  FXL_VLOG(1) << "ExceptionPort I/O thread started";

  zx_handle_t eport;
  {
    lock_guard<mutex> lock(eport_mutex_);
    eport = eport_handle_.get();
  }
  while (keep_running_) {
    zx_port_packet_t packet;
    zx_status_t status =
        zx_port_wait(eport, ZX_TIME_INFINITE, &packet, 0);
    if (status < 0) {
      FXL_LOG(ERROR) << "zx_port_wait returned error: "
                     << util::ZxErrorString(status);
    }

    FXL_VLOG(2) << "IO port packet received - key: " << packet.key
                << " type: " << IOPortPacketTypeToString(packet);

    // TODO(armansito): How to handle this?
    if (!ZX_PKT_IS_EXCEPTION(packet.type))
      continue;

    FXL_VLOG(1) << "Exception received: "
                << util::ExceptionName(static_cast<const zx_excp_type_t>(
                       packet.type))
                << " (" << packet.type
                << "), pid: " << packet.exception.pid
                << ", tid: " << packet.exception.tid;

    // Handle the exception on the main thread.
    origin_task_runner_->PostTask([packet, this] {
      const auto& iter = callbacks_.find(packet.key);
      if (iter == callbacks_.end()) {
        FXL_VLOG(1) << "No handler registered for exception";
        return;
      }

      zx_exception_report_t report;

      if (ZX_EXCP_IS_ARCH(packet.type)) {
        // TODO(dje): We already maintain a table of threads plus their
        // handles. Rewrite this to work with that table. Now would be a fine
        // time to notice new threads, but for existing threads there's no
        // point in doing a lookup to get a new handle.
        zx_handle_t thread;
        zx_status_t status = zx_object_get_child(iter->second.process_handle,
                                                 packet.exception.tid,
                                                 ZX_RIGHT_SAME_RIGHTS,
                                                 &thread);
        if (status < 0) {
          FXL_VLOG(1) << "Failed to get a handle to [" << packet.exception.pid
                      << "." << packet.exception.tid << "]";
          return;
        }
        status = zx_object_get_info(thread, ZX_INFO_THREAD_EXCEPTION_REPORT,
                                    &report, sizeof(report), NULL, NULL);
        zx_handle_close(thread);
        if (status < 0) {
          FXL_VLOG(1) << "Failed to get exception report for [" << packet.exception.pid
                      << "." << packet.exception.tid << "]";
          return;
        }
      } else {
        // Fill in |report| for a synthetic exception.
        memset(&report, 0, sizeof(report));
        report.header.size = sizeof(report);
        report.header.type = packet.type;
      }

      iter->second.callback(packet, report.context);
    });
  }

  // Close the I/O port.
  {
    lock_guard<mutex> lock(eport_mutex_);
    eport_handle_.reset();
  }
}

void PrintException(FILE* out, Process* process, Thread* thread,
                    zx_excp_type_t type,
                    const zx_exception_context_t& context) {
  if (ZX_EXCP_IS_ARCH(type)) {
    fprintf(out, "Thread %s received exception %s\n",
            thread->GetDebugName().c_str(),
            util::ExceptionToString(type, context).c_str());
    zx_vaddr_t pc = thread->registers()->GetPC();
    fprintf(out, "PC 0x%" PRIxPTR "\n", pc);
  } else {
    switch (type) {
    case ZX_EXCP_THREAD_STARTING:
      fprintf(out, "Thread %s is starting\n", thread->GetDebugName().c_str());
      break;
    case ZX_EXCP_THREAD_EXITING:
      fprintf(out, "Thread %s is exiting\n", thread->GetDebugName().c_str());
      break;
    case ZX_EXCP_GONE:
      if (thread)
        fprintf(out, "Thread %s is gone\n", thread->GetDebugName().c_str());
      else
        fprintf(out, "Process %s is gone, rc %d\n",
                process->GetName().c_str(), process->ExitCode());
      break;
    default:
      fprintf(out, "Unknown exception %u\n", type);
      break;
    }
  }
}

}  // namespace debugserver
