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
      debug_data_(dispatcher, std::move(data_processor)),
      moniker_url_cache_(zx::sec(10), dispatcher) {
  binding_.Bind(std::move(request), dispatcher);
}

void EventStreamImpl::BindDebugData(std::string moniker, std::string url,
                                    fuchsia::sys2::Event event) {
  auto chan = zx::channel(event.mutable_event_result()
                              ->payload()
                              .capability_requested()
                              .mutable_capability()
                              ->release());
  debug_data_.BindChannel(std::move(chan), std::move(moniker), std::move(url));
}

void EventStreamImpl::OnEvent(fuchsia::sys2::Event event) {
  auto type = event.header().event_type();

  if (type == fuchsia::sys2::EventType::CAPABILITY_REQUESTED) {
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
