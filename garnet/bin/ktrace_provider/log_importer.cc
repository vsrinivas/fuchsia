// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ktrace_provider/log_importer.h"

#include <fuchsia/boot/c/fidl.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-engine/instrumentation.h>
#include <lib/zx/channel.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

namespace ktrace_provider {

LogImporter::LogImporter() = default;

LogImporter::~LogImporter() { Stop(); }

void LogImporter::Start() {
  if (log_)
    return;

  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create channel";
    return;
  }
  constexpr char kReadOnlyLogPath[] = "/svc/" fuchsia_boot_ReadOnlyLog_Name;
  status = fdio_service_connect(kReadOnlyLogPath, remote.release());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to connect to ReadOnlyLog";
    return;
  }

  status = fuchsia_boot_ReadOnlyLogGet(local.get(), log_.reset_and_get_address());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "ReadOnlyLogGet failed";
    return;
  }

  start_time_ = zx_clock_get_monotonic();
  time_scale_ = static_cast<double>(zx_ticks_per_second()) / 1'000'000'000.0;

  wait_.set_object(log_.get());
  wait_.set_trigger(ZX_LOG_READABLE);
  status = wait_.Begin(async_get_default_dispatcher());
  FX_CHECK(status == ZX_OK) << "status=" << status;
}

void LogImporter::Stop() {
  if (!log_)
    return;

  zx_status_t status = wait_.Cancel();
  FX_CHECK(status == ZX_OK) << "status=" << status;

  log_.reset();
}

void LogImporter::Handle(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                         const zx_packet_signal_t* signal) {
  if (status != ZX_OK)
    return;

  alignas(zx_log_record_t) char log_buffer[ZX_LOG_RECORD_MAX];
  zx_log_record_t* log_record = reinterpret_cast<zx_log_record_t*>(log_buffer);

  for (;;) {
    zx_status_t status = log_.read(/*options=*/0, /*buffer=*/log_record,
                                   /*buffer_size=*/ZX_LOG_RECORD_MAX);
    if (status == ZX_ERR_SHOULD_WAIT)
      break;
    FX_CHECK(status >= ZX_OK) << "status=" << status;

    if (log_record->timestamp < start_time_)
      continue;

    if (auto context = trace_acquire_context()) {
      trace_thread_ref_t thread_ref =
          trace_make_inline_thread_ref(log_record->pid, log_record->tid);
      trace_context_write_log_record(
          context,
          static_cast<trace_ticks_t>(static_cast<double>(log_record->timestamp) * time_scale_),
          &thread_ref, log_record->data, log_record->datalen);
      trace_release_context(context);
    }
  }

  wait->Begin(dispatcher);  // ignore errors
}

}  // namespace ktrace_provider
