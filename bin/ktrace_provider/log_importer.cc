// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/src/ktrace_provider/log_importer.h"

#include <magenta/syscalls.h>
#include <magenta/syscalls/log.h>
#include <trace-engine/instrumentation.h>

#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace ktrace_provider {

LogImporter::LogImporter() {
  wait_.set_handler(mxtl::BindMember(this, &LogImporter::Handle));
}

LogImporter::~LogImporter() {
  Stop();
}

void LogImporter::Start() {
  if (log_)
    return;

  mx_status_t status = mx::log::create(&log_, MX_LOG_FLAG_READABLE);
  if (status != MX_OK) {
    FTL_LOG(ERROR) << "Failed to open kernel log: status=" << status;
    return;
  }

  start_ticks_ = mx_ticks_get();
  start_time_ = mx_time_get(MX_CLOCK_MONOTONIC);

  wait_.set_object(log_.get());
  wait_.set_trigger(MX_LOG_READABLE);
  status = wait_.Begin(mtl::MessageLoop::GetCurrent()->async());
  FTL_CHECK(status == MX_OK) << "status=" << status;
}

void LogImporter::Stop() {
  if (!log_)
    return;

  mx_status_t status = wait_.Cancel(mtl::MessageLoop::GetCurrent()->async());
  FTL_CHECK(status == MX_OK) << "status=" << status;

  log_.reset();
}

async_wait_result_t LogImporter::Handle(async_t* async,
                                        mx_status_t status,
                                        const mx_packet_signal_t* signal) {
  alignas(mx_log_record_t) char log_buffer[MX_LOG_RECORD_MAX];
  mx_log_record_t* log_record = reinterpret_cast<mx_log_record_t*>(log_buffer);

  for (;;) {
    mx_status_t status = log_.read(MX_LOG_RECORD_MAX, log_record, 0);
    if (status == MX_ERR_SHOULD_WAIT)
      break;
    FTL_CHECK(status >= MX_OK) << "status=" << status;

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
