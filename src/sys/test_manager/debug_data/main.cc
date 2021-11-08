// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/sys2/cpp/fidl.h>
#include <fuchsia/test/internal/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/syslog/global.h>

#include <memory>

#include <fbl/unique_fd.h>
#include <src/lib/files/directory.h>

#include "data_processor.h"
#include "event_stream.h"

using TakeStaticEventStream_Result = fuchsia::sys2::EventSource_TakeStaticEventStream_Result;

int main(int argc, const char** argv) {
  // diagnostic is tagging this component_manager_dor_test by default.
  syslog::SetTags({"debug_data"});
  FX_LOGS(INFO) << "Started debug data processor";
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  auto event_source = context->svc()->Connect<fuchsia::sys2::EventSource>();
  std::unique_ptr<EventStreamImpl> event_stream_impl;
  auto dispatcher = loop.dispatcher();

  int fd;
  if ((fd = open("/data", O_DIRECTORY | O_RDWR)) == -1) {
    FX_LOGS(ERROR) << "error opening /data: " << strerror(errno);
    return -1;
  }

  fbl::unique_fd debug_data_fd(fd);

  async::Loop data_processor_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  data_processor_loop.StartThread();

  auto data_processor_dispatcher = data_processor_loop.dispatcher();

  event_source->TakeStaticEventStream(
      "EventStream", [dispatcher, data_processor_dispatcher, &event_stream_impl,
                      debug_data_fd = std::move(debug_data_fd), svc = context->svc()](
                         TakeStaticEventStream_Result event_stream_result) mutable {
        if (event_stream_result.is_err()) {
          FX_LOGS(ERROR) << "Can't connect to event stream: "
                         << std::to_string(static_cast<int>(event_stream_result.err()));
          return;
        }
        auto test_info = svc->Connect<fuchsia::test::internal::Info>();
        auto event_stream = std::move(event_stream_result.response().server_end);
        std::unique_ptr<AbstractDataProcessor> data_processor =
            std::make_unique<DataProcessor>(std::move(debug_data_fd), data_processor_dispatcher);

        event_stream_impl = std::make_unique<EventStreamImpl>(
            std::move(event_stream), std::move(test_info), std::move(data_processor), dispatcher);
      });

  loop.Run();
  data_processor_loop.Quit();
}
