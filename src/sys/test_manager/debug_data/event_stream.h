// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TEST_MANAGER_DEBUG_DATA_EVENT_STREAM_H_
#define SRC_SYS_TEST_MANAGER_DEBUG_DATA_EVENT_STREAM_H_

#include <fuchsia/sys2/cpp/fidl.h>
#include <fuchsia/test/internal/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>

#include <memory>

#include "abstract_data_processor.h"
#include "cache.h"
#include "debug_data.h"

/// This class is not thread safe.
class EventStreamImpl : public fuchsia::sys2::EventStream {
 public:
  using EventstreamCallback = fit::function<void(fuchsia::sys2::Event event)>;

  explicit EventStreamImpl(fidl::InterfaceRequest<fuchsia::sys2::EventStream> request,
                           fidl::InterfacePtr<fuchsia::test::internal::Info> test_info,
                           std::unique_ptr<AbstractDataProcessor> data_processor,
                           async_dispatcher_t* dispatcher);

  void OnEvent(fuchsia::sys2::Event event) override;

 private:
  void BindDebugData(std::string moniker, std::string url, fuchsia::sys2::Event event);
  void ProcessCapabilityRequestedEvent(fuchsia::sys2::Event event);
  void ProcessComponentStopEvent(fuchsia::sys2::Event event);
  void ProcessDataSink(const std::string& moniker);

  fidl::InterfacePtr<fuchsia::test::internal::Info> test_info_;
  fidl::Binding<fuchsia::sys2::EventStream> binding_;
  DebugDataImpl debug_data_;

  /// This optimizes the case where a component makes multiple connections to debug data.
  Cache<std::string, std::string> moniker_url_cache_;

  // Keeps this cache around as debug data publish request can come out of order.
  // This is used as a set and value is always true.
  // This data structure optimizes the case where we get out of order stop event and publish event
  // on debugdata.
  Cache<std::string, bool> dead_component_cache_;

  std::unique_ptr<AbstractDataProcessor> data_processor_;
  async_dispatcher_t* dispatcher_;
};

#endif  // SRC_SYS_TEST_MANAGER_DEBUG_DATA_EVENT_STREAM_H_
