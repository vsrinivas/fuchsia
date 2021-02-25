// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TEST_MANAGER_DEBUG_DATA_EVENT_STREAM_H_
#define SRC_SYS_TEST_MANAGER_DEBUG_DATA_EVENT_STREAM_H_

#include <fuchsia/sys2/cpp/fidl.h>
#include <fuchsia/test/internal/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>

#include "debug_data.h"
#include "moniker_url_cache.h"

class EventStreamImpl : public fuchsia::sys2::EventStream {
 public:
  using EventstreamCallback = fit::function<void(fuchsia::sys2::Event event)>;

  explicit EventStreamImpl(fidl::InterfaceRequest<fuchsia::sys2::EventStream> request,
                           fidl::InterfacePtr<fuchsia::test::internal::Info> test_info,
                           async_dispatcher_t* dispatcher);

  void OnEvent(fuchsia::sys2::Event event) override;

 private:
  void BindDebugData(std::string url, fuchsia::sys2::Event event);

  fidl::InterfacePtr<fuchsia::test::internal::Info> test_info_;
  fidl::Binding<fuchsia::sys2::EventStream> binding_;
  DebugDataImpl debug_data_;
  MonikerUrlCache cache_;
  async_dispatcher_t* dispatcher_;
};

#endif  // SRC_SYS_TEST_MANAGER_DEBUG_DATA_EVENT_STREAM_H_
