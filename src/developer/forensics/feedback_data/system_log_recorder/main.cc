// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>
#include <lib/zx/time.h>
#include <zircon/processargs.h>

#include <optional>

#include "src/developer/forensics/feedback/config.h"
#include "src/developer/forensics/feedback/redactor_factory.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/controller.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/production_encoding.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/system_log_recorder.h"
#include "src/developer/forensics/utils/redact/redactor.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {

constexpr zx::duration kWritePeriod = zx::sec(1);

int main() {
  syslog::SetTags({"forensics", "feedback"});

  // We receive a channel that we interpret as a fuchsia.feedback.DataProviderController
  // connection.
  zx::channel controller_channel(zx_take_startup_handle(PA_HND(PA_USER0, 0)));
  if (!controller_channel.is_valid()) {
    FX_LOGS(FATAL) << "Received invalid controller channel";
    return EXIT_FAILURE;
  }

  // We receive a channel that we interpret as a fuchsia.process.lifecycle.Lifecycle
  // connection.
  zx::channel lifecycle_channel(zx_take_startup_handle(PA_HND(PA_USER1, 0)));
  if (!lifecycle_channel.is_valid()) {
    FX_LOGS(FATAL) << "Received invalid lifecycle channel";
    return EXIT_FAILURE;
  }

  const std::optional<feedback::BuildTypeConfig> build_type_config = feedback::GetBuildTypeConfig();
  if (!build_type_config.has_value()) {
    FX_LOGS(FATAL) << "Failed to read build type config";
    return EXIT_FAILURE;
  }

  async::Loop main_loop(&kAsyncLoopConfigAttachToCurrentThread);
  async::Loop write_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(main_loop.dispatcher(), "system_log_recorder");

  if (const zx_status_t status = write_loop.StartThread("writer-thread"); status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "Failed to start writer thread";
    return EXIT_FAILURE;
  }

  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  SystemLogRecorder recorder(
      main_loop.dispatcher(), write_loop.dispatcher(), context->svc(),
      SystemLogRecorder::WriteParameters{
          .period = kWritePeriod,
          .max_write_size = kMaxWriteSize,
          .logs_dir = kCurrentLogsDir,
          .max_num_files = kMaxNumLogFiles,
          .total_log_size = kPersistentLogsMaxSize,
      },
      // Don't set up Inspect because all messages in the previous boot log
      // are in the current boot log and counted in Inspect.
      feedback::RedactorFromConfig(nullptr /*no inspect*/, *build_type_config),
      std::unique_ptr<Encoder>(new ProductionEncoder()));

  // Set up the controller to shut down or flush the buffers of the system log recorder when it gets
  // the signal to do so.
  Controller controller(&main_loop, &write_loop, &recorder);
  ::fidl::Binding<fuchsia::feedback::DataProviderController> data_provider_controller_binding(
      &controller, std::move(controller_channel), main_loop.dispatcher());
  ::fidl::Binding<fuchsia::process::lifecycle::Lifecycle> lifecycle_binding(
      &controller, std::move(lifecycle_channel), main_loop.dispatcher());

  controller.SetStop([&] {
    recorder.Flush(kStopMessageStr);
    lifecycle_binding.Close(ZX_OK);
    // Don't stop the loop so incoming logs can be persisted while appmgr is waiting to terminate v1
    // components.
  });

  recorder.Start();

  main_loop.Run();

  FX_LOGS(INFO) << "Shutting down the system log recorder";

  return EXIT_SUCCESS;
}

}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics
