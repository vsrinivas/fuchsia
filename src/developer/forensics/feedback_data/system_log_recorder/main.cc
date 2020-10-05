// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>
#include <lib/zx/time.h>

#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/production_encoding.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/system_log_recorder.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {

constexpr zx::duration kWritePeriod = zx::sec(1);

int main() {
  syslog::SetTags({"forensics", "feedback"});

  async::Loop main_loop(&kAsyncLoopConfigAttachToCurrentThread);
  async::Loop write_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(main_loop.dispatcher(), "system_log_recorder");

  if (const zx_status_t status = write_loop.StartThread("writer-thread"); status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "Failed to start writer thread";
    return EXIT_FAILURE;
  }

  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  SystemLogRecorder recorder(write_loop.dispatcher(), context->svc(),
                             SystemLogRecorder::WriteParameters{
                                 .period = kWritePeriod,
                                 .max_write_size_bytes = kMaxWriteSizeInBytes,
                                 .logs_dir = kCurrentLogsDir,
                                 .max_num_files = kMaxNumLogFiles,
                                 .total_log_size_bytes = kPersistentLogsMaxSizeInKb * 1024,
                             },
                             std::unique_ptr<Encoder>(new ProductionEncoder()));
  recorder.Start();

  main_loop.Run();

  return EXIT_SUCCESS;
}

}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics
