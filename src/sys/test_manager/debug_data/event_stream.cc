// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "event_stream.h"

#include <fuchsia/sys2/cpp/fidl.h>
#include <fuchsia/test/internal/cpp/fidl.h>
#include <lib/async/dispatcher.h>

#include <string>

#include "lib/syslog/cpp/macros.h"

using GetTestUrl_Result = fuchsia::test::internal::Info_GetTestUrl_Result;

EventStreamImpl::EventStreamImpl(fidl::InterfaceRequest<fuchsia::sys2::EventStream> request,
                                 fidl::InterfacePtr<fuchsia::test::internal::Info> test_info,
                                 std::unique_ptr<AbstractDataProcessor> data_processor,
                                 async_dispatcher_t* dispatcher)
    : test_info_(std::move(test_info)),
      binding_(this),
      moniker_url_cache_(zx::sec(10), dispatcher),
      dead_component_cache_(zx::min(1), dispatcher),
      data_processor_(std::move(data_processor)),
      dispatcher_(dispatcher) {
  binding_.Bind(std::move(request), dispatcher);
}

void EventStreamImpl::BindDebugData(std::string moniker, std::string url,
                                    fuchsia::sys2::Event event) {
  auto chan = zx::channel(event.mutable_event_result()
                              ->payload()
                              .capability_requested()
                              .mutable_capability()
                              ->release());
  debug_data_.BindChannel(std::move(chan), std::move(moniker), std::move(url), dispatcher_,
                          [this](const std::string& moniker) {
                            auto dead = dead_component_cache_.GetValue(moniker);
                            // Component was already stopped so all debug data is ready to process.
                            if (dead.has_value() && dead.value()) {
                              ProcessDataSink(moniker);
                            }
                          });
}

void EventStreamImpl::OnEvent(fuchsia::sys2::Event event) {
  auto type = event.header().event_type();

  if (type == fuchsia::sys2::EventType::STOPPED) {
    ProcessComponentStopEvent(std::move(event));
  } else if (type == fuchsia::sys2::EventType::CAPABILITY_REQUESTED) {
    ProcessCapabilityRequestedEvent(std::move(event));
  } else {
    FX_LOGS(FATAL) << "got invalid event: " << static_cast<uint32_t>(type);
  }
}

void EventStreamImpl::ProcessCapabilityRequestedEvent(fuchsia::sys2::Event event) {
  FX_LOGS(DEBUG) << "Handling capability request from " << event.header().moniker();

  auto component_url_value = moniker_url_cache_.GetValue(event.header().moniker());
  if (component_url_value.has_value()) {
    BindDebugData(event.header().moniker(), *component_url_value, std::move(event));
    return;
  }

  test_info_->GetTestUrl(
      event.header().moniker(), [this, event = std::move(event)](GetTestUrl_Result result) mutable {
        std::string url;
        if (result.is_err()) {
          FX_LOGS(WARNING) << "URL for " << event.header().moniker() << " not found.";
        } else {
          url = result.response().ResultValue_();
        }
        moniker_url_cache_.Add(event.header().moniker(), url);
        BindDebugData(event.header().moniker(), std::move(url), std::move(event));
      });
}

void EventStreamImpl::ProcessComponentStopEvent(fuchsia::sys2::Event event) {
  // TODO(http://fxbug.dev/71952): We don't expect to receive stop event more than once for a test
  // because of how test manager handles running test. The code below assumes this assumption and
  // should be fixed if the assumption is no longer valid.

  FX_LOGS(DEBUG) << "Handling stop event from " << event.header().moniker();
  dead_component_cache_.Add(event.header().moniker(), true);
  // Process current debug data. If there are Publish requests in the channel that haven't been
  // processed yet, they would be handled by callback in `BindChannel` call above.
  ProcessDataSink(event.header().moniker());
}

void EventStreamImpl::ProcessDataSink(const std::string& moniker) {
  auto debug_info = debug_data_.TakeData(moniker);
  if (debug_info.has_value()) {
    data_processor_->ProcessData(std::move(debug_info->first), std::move(debug_info->second));
  }
}
