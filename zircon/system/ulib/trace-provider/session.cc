// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "session.h"

#include <inttypes.h>
#include <lib/async/cpp/task.h>
#include <lib/trace-provider/provider.h>
#include <lib/zx/vmar.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <utility>

#include "utils.h"

namespace trace {
namespace internal {

Session::Session(async_dispatcher_t* dispatcher, void* buffer, size_t buffer_num_bytes,
                 zx::fifo fifo, std::vector<std::string> categories)
    : dispatcher_(dispatcher),
      buffer_(buffer),
      buffer_num_bytes_(buffer_num_bytes),
      fifo_(std::move(fifo)),
      fifo_wait_(this, fifo_.get(), ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED),
      categories_(std::move(categories)) {
  // Build a quick lookup table for IsCategoryEnabled().
  for (const auto& cat : categories_) {
    enabled_category_set_.emplace(StringSetEntry(cat.c_str()));
  }
}

Session::~Session() {
  zx_status_t status =
      zx::vmar::root_self()->unmap(reinterpret_cast<uintptr_t>(buffer_), buffer_num_bytes_);
  ZX_DEBUG_ASSERT(status == ZX_OK);
  status = fifo_wait_.Cancel();
  ZX_DEBUG_ASSERT(status == ZX_OK || status == ZX_ERR_NOT_FOUND);
}

void Session::InitializeEngine(async_dispatcher_t* dispatcher,
                               trace_buffering_mode_t buffering_mode, zx::vmo buffer, zx::fifo fifo,
                               std::vector<std::string> categories) {
  ZX_DEBUG_ASSERT(buffer);
  ZX_DEBUG_ASSERT(fifo);

  // If the engine isn't stopped flag an error. No one else should be
  // starting/stopping the engine so testing this here is ok.
  switch (trace_state()) {
    case TRACE_STOPPED:
      break;
    case TRACE_STOPPING:
      fprintf(stderr,
              "Session for process %" PRIu64
              ": cannot initialize engine, still stopping from previous trace\n",
              GetPid());
      return;
    case TRACE_STARTED:
      // We can get here if the app errantly tried to create two providers.
      // This is a bug in the app, provide extra assistance for diagnosis.
      // Including the pid here has been extraordinarily helpful.
      fprintf(stderr,
              "Session for process %" PRIu64
              ": engine is already initialized. Is there perchance two"
              " providers in this app?\n",
              GetPid());
      return;
    default:
      __UNREACHABLE;
  }

  uint64_t buffer_num_bytes;
  zx_status_t status = buffer.get_size(&buffer_num_bytes);
  if (status != ZX_OK) {
    fprintf(stderr, "Session: error getting buffer size, status=%d(%s)\n", status,
            zx_status_get_string(status));
    return;
  }

  uintptr_t buffer_ptr;
  status = zx::vmar::root_self()->map(0u, buffer, 0u, buffer_num_bytes,
                                      ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &buffer_ptr);
  if (status != ZX_OK) {
    fprintf(stderr, "Session: error mapping buffer, status=%d(%s)\n", status,
            zx_status_get_string(status));
    return;
  }

  auto session = new Session(dispatcher, reinterpret_cast<void*>(buffer_ptr), buffer_num_bytes,
                             std::move(fifo), std::move(categories));

  status = session->fifo_wait_.Begin(dispatcher);
  if (status != ZX_OK) {
    fprintf(stderr, "Session: error starting fifo wait, status=%d(%s)\n", status,
            zx_status_get_string(status));
    delete session;
    return;
  }

  status = trace_engine_initialize(dispatcher, session, buffering_mode, session->buffer_,
                                   session->buffer_num_bytes_);
  if (status != ZX_OK) {
    fprintf(stderr, "Session: error starting engine, status=%d(%s)\n", status,
            zx_status_get_string(status));
    delete session;
  } else {
    // The session will be destroyed in |TraceTerminated()|.
  }
}

void Session::StartEngine(trace_start_mode_t start_mode) {
  // If the engine isn't stopped flag an error. No one else should be
  // starting/stopping the engine so testing this here is ok.
  switch (trace_state()) {
    case TRACE_STOPPED:
      break;
    case TRACE_STOPPING:
      fprintf(stderr,
              "Session for process %" PRIu64
              ": cannot start engine, still stopping from previous trace\n",
              GetPid());
      return;
    case TRACE_STARTED:
      // Ignore.
      return;
    default:
      __UNREACHABLE;
  }

  zx_status_t status = trace_engine_start(start_mode);
  if (status != ZX_OK) {
    // There's nothing more we can do here. There's currently no easy way
    // to inform trace-manager of the error because we don't have a copy
    // of "this", we're a static method and we give ownership of "this" to
    // the trace engine. When the trace-engine wants to invoke one of our
    // methods it does so via the "handler" API. But we're not in the
    // engine here. Fortunately there's nothing more we need to do here.
    // The kinds of errors we can get here fall into three categories:
    // 1) Can't happen: If it can't happen then just let trace-manager
    //    timeout waiting for us to start.
    // 2) To be ignored: The FIDL provider protocol specifies that if a
    //    Start request is received when the engine is not stopped then the
    //    request is to be ignored.
    // 3) Async loop shutting down: If the provider is shutting down its
    //    async loop then the engine is about to be terminated anyway.
    //
    // Just log the error for debugging purposes.
    // Ignore BAD_STATE as that is case #2.
    if (status != ZX_ERR_BAD_STATE) {
      fprintf(stderr, "Session: error starting engine, status=%d(%s)\n", status,
              zx_status_get_string(status));
    }
  }
}

void Session::StopEngine() { trace_engine_stop(ZX_OK); }

void Session::TerminateEngine() { trace_engine_terminate(); }

void Session::HandleFifo(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                         const zx_packet_signal_t* signal) {
  if (status == ZX_ERR_CANCELED) {
    // The wait could be canceled if we're shutting down, e.g., the
    // program is exiting.
    return;
  }

  if (status != ZX_OK) {
    fprintf(stderr, "Session: FIFO wait failed: status=%d(%s)\n", status,
            zx_status_get_string(status));
  } else if (signal->observed & ZX_FIFO_READABLE) {
    if (ReadFifoMessage()) {
      zx_status_t status = wait->Begin(dispatcher);
      if (status == ZX_OK) {
        return;
      }
      fprintf(stderr, "Session: Error re-registering FIFO wait: status=%d(%s)\n", status,
              zx_status_get_string(status));
    }
  } else {
    ZX_DEBUG_ASSERT(signal->observed & ZX_FIFO_PEER_CLOSED);
  }

  // TraceManager is gone or other error with the fifo.
  TerminateEngine();
}

bool Session::ReadFifoMessage() {
  trace_provider_packet_t packet;
  auto status = fifo_.read(sizeof(packet), &packet, 1u, nullptr);
  ZX_DEBUG_ASSERT(status == ZX_OK);
  if (packet.data16 != 0) {
    fprintf(stderr, "Session: data16 field non-zero from TraceManager: %u\n", packet.data16);
    return false;
  }
  switch (packet.request) {
    case TRACE_PROVIDER_BUFFER_SAVED: {
      auto wrapped_count = packet.data32;
      auto durable_data_end = packet.data64;
#if 0  // TODO(fxbug.dev/22887): Don't delete this, save for conversion to syslog.
        fprintf(stderr, "Session: Received buffer_saved message"
                ", wrapped_count=%u, durable_data_end=0x%" PRIx64 "\n",
                wrapped_count, durable_data_end);
#endif
      status = MarkBufferSaved(wrapped_count, durable_data_end);
      if (status == ZX_ERR_BAD_STATE) {
        // This happens when tracing has stopped. Ignore it.
      } else if (status != ZX_OK) {
        fprintf(stderr, "Session: MarkBufferSaved failed: status=%d\n", status);
        return false;
      }
      break;
    }
    default:
      fprintf(stderr, "Session: Bad request from TraceManager: %u\n", packet.request);
      return false;
  }

  return true;
}

zx_status_t Session::MarkBufferSaved(uint32_t wrapped_count, uint64_t durable_data_end) {
  return trace_engine_mark_buffer_saved(wrapped_count, durable_data_end);
}

bool Session::IsCategoryEnabled(const char* category) {
  if (categories_.size() == 0) {
    // If none are specified, enable all categories.
    return true;
  }
  return enabled_category_set_.find(StringSetEntry(category)) != enabled_category_set_.end();
}

void Session::SendFifoPacket(const trace_provider_packet_t* packet) {
  auto status = fifo_.write(sizeof(*packet), packet, 1, nullptr);
  ZX_DEBUG_ASSERT(status == ZX_OK || status == ZX_ERR_PEER_CLOSED);
}

void Session::TraceStarted() {
  trace_provider_packet_t packet{};
  packet.request = TRACE_PROVIDER_STARTED;
  packet.data32 = TRACE_PROVIDER_FIFO_PROTOCOL_VERSION;
  SendFifoPacket(&packet);
}

void Session::TraceStopped(zx_status_t disposition) {
  trace_provider_packet_t packet{};
  packet.request = TRACE_PROVIDER_STOPPED;
  SendFifoPacket(&packet);
}

void Session::TraceTerminated() {
  // Destruction can race with HandleFifo, e.g., if the dispatcher runs on a background thread
  // and tracing terminates on a different thread. Handle this by running the destructor on the
  // dispatcher's thread (which we assume is single-threaded). It may also happen that the task
  // is not run. This can happen if the loop is quit and destructed before the task is run.
  // Handle this by letting destruction of the closure delete the session.
  std::unique_ptr<Session> session{this};
  async::PostTask(dispatcher_, [session = std::move(session)]() {});
}

void Session::NotifyBufferFull(uint32_t wrapped_count, uint64_t durable_data_end) {
  trace_provider_packet_t packet{};
  packet.request = TRACE_PROVIDER_SAVE_BUFFER;
  packet.data32 = wrapped_count;
  packet.data64 = durable_data_end;
  auto status = fifo_.write(sizeof(packet), &packet, 1, nullptr);
  // There's something wrong in our protocol or implementation if we fill
  // the fifo buffer.
  ZX_DEBUG_ASSERT(status == ZX_OK || status == ZX_ERR_PEER_CLOSED);
}

void Session::SendAlert(const char* alert_name) {
  trace_provider_packet_t packet{};

  size_t alert_name_length = strlen(alert_name);
  if (alert_name_length > sizeof(packet.data16) + sizeof(packet.data32) + sizeof(packet.data64)) {
    fprintf(stderr, "Session: Alert name too long: %s\n", alert_name);
    return;
  }

  packet.request = TRACE_PROVIDER_ALERT;
  memcpy(&packet.data16, alert_name, alert_name_length);
  auto status = fifo_.write(sizeof(packet), &packet, 1, nullptr);
  ZX_DEBUG_ASSERT(status == ZX_OK || status == ZX_ERR_PEER_CLOSED);
}

}  // namespace internal
}  // namespace trace
