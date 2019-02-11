// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "exception_port.h"

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

namespace inferior_control {

// static
ExceptionPort::Key ExceptionPort::g_key_counter = 0;

ExceptionPort::ExceptionPort(async_dispatcher_t* dispatcher)
    : keep_running_(false), origin_dispatcher_(dispatcher) {
  FXL_DCHECK(origin_dispatcher_);
}

ExceptionPort::~ExceptionPort() {
  if (eport_)
    Quit();
}

bool ExceptionPort::Run() {
  FXL_DCHECK(!eport_);
  FXL_DCHECK(!keep_running_);

  // Create the port used to bind to the inferior's exception port.
  // TODO(dje): We can use a provided async loop once ports are no longer
  // used to bind to exception ports.
  zx_status_t status = zx::port::create(0, &eport_);
  if (status < 0) {
    FXL_LOG(ERROR) << "Failed to create the exception port: "
                   << debugger_utils::ZxErrorString(status);
    return false;
  }

  FXL_DCHECK(eport_);

  keep_running_ = true;
  port_thread_ = std::thread(fit::bind_member(this, &ExceptionPort::Worker));

  return true;
}

void ExceptionPort::Quit() {
  FXL_DCHECK(eport_);
  FXL_DCHECK(keep_running_);

  FXL_LOG(INFO) << "Quitting exception port loop";

  keep_running_ = false;

  // This is called from a different thread than |port_thread_|.
  // Send it a packet waking it up. It will notice |keep_running_ == false|
  // and exit.
  zx_port_packet_t packet{};
  // We don't use USER packets for anything else yet.
  packet.type = ZX_PKT_TYPE_USER;
  eport_.queue(&packet);

  port_thread_.join();

  FXL_LOG(INFO) << "Exception port loop exited";
}

ExceptionPort::Key ExceptionPort::Bind(zx_handle_t process_handle,
                                       Callback callback) {
  FXL_DCHECK(process_handle != ZX_HANDLE_INVALID);
  FXL_DCHECK(callback);
  FXL_DCHECK(eport_);

  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(process_handle, ZX_INFO_HANDLE_BASIC, &info,
                         sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_object_get_info_failed: "
                   << debugger_utils::ZxErrorString(status);
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

  status = zx_task_bind_exception_port(process_handle, eport_.get(),
                                       next_key, ZX_EXCEPTION_PORT_DEBUGGER);
  if (status < 0) {
    FXL_LOG(ERROR) << "Failed to bind exception port: "
                   << debugger_utils::ZxErrorString(status);
    return 0;
  }

  // Also watch for process terminated signals.
  status = zx_object_wait_async(process_handle, eport_.get(), next_key,
                                ZX_TASK_TERMINATED, ZX_WAIT_ASYNC_ONCE);
  if (status < 0) {
    FXL_LOG(ERROR) << "Failed to async wait for process: "
                   << debugger_utils::ZxErrorString(status);
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
  FXL_DCHECK(eport_);

  // Give this thread an identifiable name for debugging purposes.
  fsl::SetCurrentThreadName("exception port reader");

  FXL_VLOG(1) << "Exception port thread started";

  while (keep_running_) {
    zx_port_packet_t packet;
    zx_status_t status = eport_.wait(zx::time::infinite(), &packet);
    if (status < 0) {
      FXL_LOG(ERROR) << "zx_port_wait returned error: "
                     << debugger_utils::ZxErrorString(status);
      // We're no longer running, record it.
      keep_running_ = false;
      break;
    }

    if (ZX_PKT_IS_EXCEPTION(packet.type)) {
      FXL_VLOG(2) << "Received exception: "
                  << debugger_utils::ExceptionName(
                         static_cast<const zx_excp_type_t>(packet.type))
                  << " (" << packet.type << "), key=" << packet.key
                  << " pid=" << packet.exception.pid
                  << " tid=" << packet.exception.tid;
    } else if (packet.type == ZX_PKT_TYPE_SIGNAL_ONE) {
      FXL_VLOG(2) << "Received signal:"
                  << " key=" << packet.key
                  << " trigger=0x" << std::hex << packet.signal.trigger
                  << " observed=0x" << std::hex << packet.signal.observed;
    } else if (packet.type == ZX_PKT_TYPE_USER) {
      // Sent to wake up the port wait because we're exiting.
      FXL_VLOG(2) << "Received user packet";
      FXL_DCHECK(!keep_running_);
      continue;
    } else {
      FXL_LOG(WARNING) << "Received unexpected packet: type="
                       << packet.type << ", key=" << packet.key;
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

  eport_.reset();
}

}  // namespace inferior_control
