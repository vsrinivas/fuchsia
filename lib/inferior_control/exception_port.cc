// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "exception_port.h"

#include <cinttypes>
#include <string>

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fit/function.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

#include "lib/fsl/handles/object_info.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "garnet/lib/debugger_utils/util.h"

#include "thread.h"

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

ExceptionPort::ExceptionPort(async_dispatcher_t* dispatcher)
    : keep_running_(false), origin_dispatcher_(dispatcher) {
  FXL_DCHECK(origin_dispatcher_);
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
                   << ZxErrorString(status);
    return false;
  }

  FXL_DCHECK(eport_handle_);

  keep_running_ = true;
  io_thread_ = std::thread(fit::bind_member(this, &ExceptionPort::Worker));

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
    eport_handle_.queue(&packet);
  }

  io_thread_.join();

  FXL_LOG(INFO) << "Exception port I/O loop exited";
}

ExceptionPort::Key ExceptionPort::Bind(zx_handle_t process_handle,
                                       Callback callback) {
  FXL_DCHECK(process_handle != ZX_HANDLE_INVALID);
  FXL_DCHECK(callback);
  FXL_DCHECK(eport_handle_);

  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(process_handle, ZX_INFO_HANDLE_BASIC, &info,
                         sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_object_get_info_failed: " << ZxErrorString(status);
    return 0;
  }
  FXL_DCHECK(info.type == ZX_OBJ_TYPE_PROCESS);
  zx_koid_t process_koid = info.koid;

  Key next_key = g_key_counter + 1;

  // Check for overflows. We don't keep track of which keys are ready to use and
  // which aren't. A 64-bit range is pretty big, so if we run out, we run out.
  if (!next_key) {
    FXL_LOG(ERROR) << "Ran out of keys!";
    return 0;
  }

  status = zx_task_bind_exception_port(process_handle, eport_handle_.get(),
                                       next_key, ZX_EXCEPTION_PORT_DEBUGGER);
  if (status < 0) {
    FXL_LOG(ERROR) << "Failed to bind exception port: "
                   << ZxErrorString(status);
    return 0;
  }

  // Also watch for process terminated signals.
  status = zx_object_wait_async(process_handle, eport_handle_.get(), next_key,
                                ZX_TASK_TERMINATED, ZX_WAIT_ASYNC_ONCE);
  if (status < 0) {
    FXL_LOG(ERROR) << "Failed to async wait for process: "
                   << ZxErrorString(status);
    return 0;
  }

  // |next_key| should not have been used before.
  FXL_DCHECK(callbacks_.find(next_key) == callbacks_.end());

  callbacks_[next_key] =
      BindData(process_handle, process_koid, std::move(callback));
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
    zx_status_t status = zx_port_wait(eport, ZX_TIME_INFINITE, &packet);
    if (status < 0) {
      FXL_LOG(ERROR) << "zx_port_wait returned error: "
                     << ZxErrorString(status);
    }

    FXL_VLOG(2) << "IO port packet received - key: " << packet.key
                << " type: " << IOPortPacketTypeToString(packet);

    if (ZX_PKT_IS_EXCEPTION(packet.type)) {
      FXL_VLOG(1) << "Exception received: "
                  << ExceptionName(
                         static_cast<const zx_excp_type_t>(packet.type))
                  << " (" << packet.type << "), pid: " << packet.exception.pid
                  << ", tid: " << packet.exception.tid;
    } else if (packet.type == ZX_PKT_TYPE_SIGNAL_ONE) {
      FXL_VLOG(1) << "Signal received:"
                  << " trigger=0x" << std::hex << packet.signal.trigger
                  << " observed=0x" << std::hex << packet.signal.observed;
    } else if (packet.type == ZX_PKT_TYPE_USER) {
      // Sent when exiting loop, just ignore.
      continue;
    } else {
      FXL_LOG(WARNING) << "Unexpected packet type: " << packet.type;
      continue;
    }

    // Handle the exception/signal on the main thread.
    async::PostTask(origin_dispatcher_, [packet, this] {
      const auto& iter = callbacks_.find(packet.key);
      if (iter == callbacks_.end()) {
        FXL_VLOG(1) << "No handler registered for exception";
        return;
      }

      zx_exception_report_t report;

      if (packet.type == ZX_PKT_TYPE_SIGNAL_ONE) {
        // Process terminated.
        memset(&report, 0, sizeof(report));
      } else if (ZX_EXCP_IS_ARCH(packet.type)) {
        // TODO(dje): We already maintain a table of threads plus their
        // handles. Rewrite this to work with that table. Now would be a fine
        // time to notice new threads, but for existing threads there's no
        // point in doing a lookup to get a new handle.
        zx_handle_t thread;
        zx_status_t status = zx_object_get_child(iter->second.process_handle,
                                                 packet.exception.tid,
                                                 ZX_RIGHT_SAME_RIGHTS, &thread);
        if (status < 0) {
          FXL_VLOG(1) << "Failed to get a handle to [" << packet.exception.pid
                      << "." << packet.exception.tid << "]";
          return;
        }
        status = zx_object_get_info(thread, ZX_INFO_THREAD_EXCEPTION_REPORT,
                                    &report, sizeof(report), NULL, NULL);
        zx_handle_close(thread);
        if (status < 0) {
          FXL_VLOG(1) << "Failed to get exception report for ["
                      << packet.exception.pid << "." << packet.exception.tid
                      << "]";
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

void PrintException(FILE* out, const Thread* thread, zx_excp_type_t type,
                    const zx_exception_context_t& context) {
  if (ZX_EXCP_IS_ARCH(type)) {
    fprintf(out, "Thread %s received exception %s\n",
            thread->GetDebugName().c_str(),
            ExceptionToString(type, context).c_str());
    zx_vaddr_t pc = thread->registers()->GetPC();
    fprintf(out, "PC 0x%" PRIxPTR "\n", pc);
  } else {
    const char* thread_name = thread->GetDebugName().c_str();
    switch (type) {
      case ZX_EXCP_THREAD_STARTING:
        fprintf(out, "Thread %s is starting\n", thread_name);
        break;
      case ZX_EXCP_THREAD_EXITING:
        fprintf(out, "Thread %s is exiting\n", thread_name);
        break;
      case ZX_EXCP_POLICY_ERROR:
        fprintf(out, "Thread %s got policy error\n", thread_name);
        break;
      default:
        fprintf(out, "Unknown exception %u\n", type);
        break;
    }
  }
}

void PrintSignals(FILE* out, const Thread* thread, zx_signals_t signals) {
  std::string description;
  if (signals & ZX_THREAD_RUNNING)
    description += ", running";
  if (signals & ZX_THREAD_SUSPENDED)
    description += ", suspended";
  if (signals & ZX_THREAD_TERMINATED)
    description += ", terminated";
  zx_signals_t mask =
      (ZX_THREAD_RUNNING | ZX_THREAD_SUSPENDED | ZX_THREAD_TERMINATED);
  if (signals & ~mask)
    description += fxl::StringPrintf(", unknown (0x%x)", signals & ~mask);
  if (description.length() == 0)
    description = ", none";
  fprintf(out, "Thread %s got signals: %s\n", thread->GetDebugName().c_str(),
          description.c_str() + 2);
}

}  // namespace debugserver
