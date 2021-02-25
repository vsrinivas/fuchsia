// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys2/cpp/fidl.h>
#include <fuchsia/test/internal/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include "event_stream.h"

using TakeStaticEventStream_Result = fuchsia::sys2::EventSource_TakeStaticEventStream_Result;

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  auto event_source = context->svc()->Connect<fuchsia::sys2::EventSource>();
  std::unique_ptr<EventStreamImpl> event_stream_impl;
  auto dispatcher = loop.dispatcher();

  event_source->TakeStaticEventStream(
      "EventStream", [dispatcher, &event_stream_impl,
                      svc = context->svc()](TakeStaticEventStream_Result event_stream_result) {
        if (event_stream_result.is_err()) {
          FX_LOGS(ERROR) << "Can't connect to event stream: "
                         << std::to_string(static_cast<int>(event_stream_result.err()));
          return;
        }
        auto test_info = svc->Connect<fuchsia::test::internal::Info>();
        auto event_stream = std::move(event_stream_result.response().server_end);
        event_stream_impl = std::make_unique<EventStreamImpl>(std::move(event_stream),
                                                              std::move(test_info), dispatcher);
      });

  loop.Run();
}
