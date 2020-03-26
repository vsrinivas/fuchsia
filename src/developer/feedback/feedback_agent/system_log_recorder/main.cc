// Copyright 2020 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/trace-provider/provider.h>
#include <lib/zx/time.h>

#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/developer/feedback/feedback_agent/system_log_recorder/system_log_recorder.h"
#include "src/developer/feedback/utils/file_size.h"
#include "src/lib/syslog/cpp/logger.h"

constexpr zx::duration kWritePeriod = zx::sec(1);

// At most 8KB of logs will be persisted each second.
constexpr size_t kMaxWriteSizeInBytes = 8 * 1024;

int main(int argc, const char** argv) {
  using namespace feedback;

  syslog::InitLogger({"feedback"});

  async::Loop main_loop(&kAsyncLoopConfigAttachToCurrentThread);
  async::Loop write_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(main_loop.dispatcher(), "system_log_recorder");

  if (const zx_status_t status = write_loop.StartThread("writer-thread"); status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "Failed to start writer thread";
    return EXIT_FAILURE;
  }

  auto context = sys::ComponentContext::Create();

  SystemLogRecorder recorder(write_loop.dispatcher(), context->svc(),
                             SystemLogRecorder::WriteParameters{
                                 .period = kWritePeriod,
                                 .max_write_size_bytes = kMaxWriteSizeInBytes,
                                 .log_file_paths = kCurrentLogsFilePaths,
                                 .total_log_size = FileSize::Kilobytes(kPersistentLogsMaxSizeInKb),
                             });
  recorder.Start();

  main_loop.Run();

  return EXIT_SUCCESS;
}
