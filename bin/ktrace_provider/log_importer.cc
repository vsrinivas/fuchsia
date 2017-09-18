// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ktrace_provider/log_importer.h"

#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>
#include <trace-engine/instrumentation.h>

#include "lib/fxl/logging.h"
#include "lib/fsl/tasks/message_loop.h"

namespace ktrace_provider {

LogImporter::LogImporter() {
  wait_.set_handler(fbl::BindMember(this, &LogImporter::Handle));
}

LogImporter::~LogImporter() {
  Stop();
}

void LogImporter::Start() {
  if (log_)
    return;

  zx_status_t status = zx::log::create(&log_, ZX_LOG_FLAG_READABLE);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to open kernel log: status=" << status;
    return;
  }

  start_ticks_ = zx_ticks_get();
  start_time_ = zx_time_get(ZX_CLOCK_MONOTONIC);

  wait_.set_object(log_.get());
  wait_.set_trigger(ZX_LOG_READABLE);
  status = wait_.Begin(fsl::MessageLoop::GetCurrent()->async());
  FXL_CHECK(status == ZX_OK) << "status=" << status;
}

void LogImporter::Stop() {
  if (!log_)
    return;

  zx_status_t status = wait_.Cancel(fsl::MessageLoop::GetCurrent()->async());
  FXL_CHECK(status == ZX_OK) << "status=" << status;

  log_.reset();
}

async_wait_result_t LogImporter::Handle(async_t* async,
                                        zx_status_t status,
                                        const zx_packet_signal_t* signal) {
  alignas(zx_log_record_t) char log_buffer[ZX_LOG_RECORD_MAX];
  zx_log_record_t* log_record = reinterpret_cast<zx_log_record_t*>(log_buffer);

  for (;;) {
    zx_status_t status = log_.read(ZX_LOG_RECORD_MAX, log_record, 0);
    if (status == ZX_ERR_SHOULD_WAIT)
      break;
    FXL_CHECK(status >= ZX_OK) << "status=" << status;

    if (log_record->timestamp < start_time_)
      continue;

    if (auto context = trace_acquire_context()) {
      trace_thread_ref_t thread_ref =
          trace_make_inline_thread_ref(log_record->pid, log_record->tid);
      trace_context_write_log_record(
          context, log_record->timestamp - start_time_ + start_ticks_,
          &thread_ref, log_record->data, log_record->datalen);
      trace_release_context(context);
    }
  }

  return ASYNC_WAIT_AGAIN;
}

}  // namespace ktrace_provider
