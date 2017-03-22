// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "exception-port.h"

#include <cinttypes>
#include <string>

#include <magenta/syscalls.h>
#include <magenta/syscalls/port.h>

#include "lib/ftl/logging.h"
#include "lib/mtl/handles/object_info.h"
#include "lib/mtl/tasks/message_loop.h"

#include "debugger-utils/util.h"

#include "process.h"

using std::lock_guard;
using std::mutex;

namespace debugserver {

namespace {

std::string IOPortPacketTypeToString(const mx_packet_header_t& header) {
#define CASE_TO_STR(x) \
  case x:              \
    return #x
  switch (header.type) {
    CASE_TO_STR(MX_PORT_PKT_TYPE_KERN);
    CASE_TO_STR(MX_PORT_PKT_TYPE_IOSN);
    CASE_TO_STR(MX_PORT_PKT_TYPE_USER);
    CASE_TO_STR(MX_PORT_PKT_TYPE_EXCEPTION);
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
  FTL_DCHECK(mtl::MessageLoop::GetCurrent());
  origin_task_runner_ = mtl::MessageLoop::GetCurrent()->task_runner();
}

ExceptionPort::~ExceptionPort() {
  if (eport_handle_)
    Quit();
}

bool ExceptionPort::Run() {
  FTL_DCHECK(!eport_handle_);
  FTL_DCHECK(!keep_running_);

  // Create an I/O port.
  mx_status_t status = mx::port::create(0u, &eport_handle_);
  if (status < 0) {
    util::LogErrorWithMxStatus("Failed to create the exception port", status);
    return false;
  }

  FTL_DCHECK(eport_handle_);

  keep_running_ = true;
  io_thread_ = std::thread(std::bind(&ExceptionPort::Worker, this));

  return true;
}

void ExceptionPort::Quit() {
  FTL_DCHECK(eport_handle_);
  FTL_DCHECK(keep_running_);

  FTL_LOG(INFO) << "Quitting exception port I/O loop";

  // Close the I/O port. This should cause mx_port_wait to return if one is
  // pending.
  keep_running_ = false;
  {
    lock_guard<mutex> lock(eport_mutex_);

    // The only way it seems possible to make the I/O thread return from
    // mx_port_wait is to queue a dummy packet on the port.
    mx_packet_header_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = MX_PORT_PKT_TYPE_USER;
    eport_handle_.queue(&packet, sizeof(packet));
  }

  io_thread_.join();

  FTL_LOG(INFO) << "Exception port I/O loop exited";
}

ExceptionPort::Key ExceptionPort::Bind(mx_handle_t process_handle,
                                       const Callback& callback) {
  FTL_DCHECK(process_handle != MX_HANDLE_INVALID);
  FTL_DCHECK(callback);
  FTL_DCHECK(eport_handle_);

  Key next_key = g_key_counter + 1;

  // Check for overflows. We don't keep track of which keys are ready to use and
  // which aren't. A 64-bit range is pretty big, so if we run out, we run out.
  if (!next_key) {
    FTL_LOG(ERROR) << "Ran out of keys!";
    return 0;
  }

  mx_status_t status =
      mx_task_bind_exception_port(process_handle, eport_handle_.get(),
                                    next_key, MX_EXCEPTION_PORT_DEBUGGER);
  if (status < 0) {
    util::LogErrorWithMxStatus("Failed to bind exception port", status);
    return 0;
  }

  // |next_key| should not have been used before.
  FTL_DCHECK(callbacks_.find(next_key) == callbacks_.end());

  callbacks_[next_key] = BindData(process_handle, callback);
  ++g_key_counter;

  FTL_VLOG(1) << "Exception port bound to process handle " << process_handle
              << " with key " << next_key;

  return next_key;
}

bool ExceptionPort::Unbind(const Key key) {
  const auto& iter = callbacks_.find(key);
  if (iter == callbacks_.end()) {
    FTL_VLOG(1) << "|key| not bound; Cannot unbind exception port";
    return false;
  }

  // Unbind the exception port. This is a best effort operation so if it fails,
  // there isn't really anything we can do to recover.
  mx_task_bind_exception_port(iter->second.process_handle, MX_HANDLE_INVALID,
                                key, MX_EXCEPTION_PORT_DEBUGGER);
  callbacks_.erase(iter);

  return true;
}

void ExceptionPort::Worker() {
  FTL_DCHECK(eport_handle_);

  // Give this thread an identifiable name for debugging purposes.
  mtl::SetCurrentThreadName("exception port reader");

  FTL_VLOG(1) << "ExceptionPort I/O thread started";

  mx_handle_t eport;
  {
    lock_guard<mutex> lock(eport_mutex_);
    eport = eport_handle_.get();
  }
  while (keep_running_) {
    mx_exception_packet_t packet;
    mx_status_t status =
        mx_port_wait(eport, MX_TIME_INFINITE, &packet, sizeof(packet));
    if (status < 0)
      util::LogErrorWithMxStatus("mx_port_wait returned error: ", status);

    FTL_VLOG(2) << "IO port packet received - key: " << packet.hdr.key
                << " type: " << IOPortPacketTypeToString(packet.hdr);

    // TODO(armansito): How to handle this?
    if (packet.hdr.type != MX_PORT_PKT_TYPE_EXCEPTION)
      continue;

    FTL_VLOG(1) << "Exception received: "
                << util::ExceptionName(static_cast<const mx_excp_type_t>(
                       packet.report.header.type))
                << " (" << packet.report.header.type
                << "), pid: " << packet.report.context.pid
                << ", tid: " << packet.report.context.tid;

    // Handle the exception on the main thread.
    origin_task_runner_->PostTask([packet, this] {
      const auto& iter = callbacks_.find(packet.hdr.key);
      if (iter == callbacks_.end()) {
        FTL_VLOG(1) << "No handler registered for exception";
        return;
      }

      iter->second.callback(
          static_cast<const mx_excp_type_t>(packet.report.header.type),
          packet.report.context);
    });
  }

  // Close the I/O port.
  {
    lock_guard<mutex> lock(eport_mutex_);
    eport_handle_.reset();
  }
}

void PrintException(FILE* out, Process* process, Thread* thread,
                    mx_excp_type_t type,
                    const mx_exception_context_t& context) {
  if (MX_EXCP_IS_ARCH(type)) {
    fprintf(out, "Thread %s received exception %s\n",
            thread->GetDebugName().c_str(),
            util::ExceptionToString(type, context).c_str());
    fprintf(out, "PC 0x%" PRIxPTR "\n", context.arch.pc);
  } else {
    switch (type) {
    case MX_EXCP_THREAD_STARTING:
      fprintf(out, "Thread %s is starting\n", thread->GetDebugName().c_str());
      break;
    case MX_EXCP_THREAD_EXITING:
      fprintf(out, "Thread %s is exiting\n", thread->GetDebugName().c_str());
      break;
    case MX_EXCP_GONE:
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
