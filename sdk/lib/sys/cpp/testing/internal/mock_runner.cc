// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/realm/builder/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fdio/include/lib/fdio/namespace.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/testing/internal/mock_runner.h>
#include <lib/sys/cpp/testing/realm_builder_types.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <memory>
#include <optional>

namespace sys::testing::internal {

namespace {

std::unique_ptr<MockHandles> CreateFromStartInfo(
    fuchsia::realm::builder::MockComponentStartInfo start_info, async_dispatcher_t* dispatcher) {
  fdio_ns_t* ns;
  ZX_ASSERT(fdio_ns_create(&ns) == ZX_OK);
  for (auto& entry : *start_info.mutable_ns()) {
    ZX_ASSERT(fdio_ns_bind(ns, entry.path().c_str(),
                           entry.mutable_directory()->TakeChannel().release()) == ZX_OK);
  }

  sys::OutgoingDirectory outgoing_dir;
  outgoing_dir.Serve(start_info.mutable_outgoing_dir()->TakeChannel(), dispatcher);

  return std::make_unique<MockHandles>(ns, std::move(outgoing_dir));
}

}  // namespace

// DNS: Add id check.
void MockRunner::Register(std::string mock_id, MockComponent* mock) {
  mocks_[std::move(mock_id)] = mock;
}

void MockRunner::Bind(fidl::InterfaceHandle<fuchsia::realm::builder::FrameworkIntermediary> handle,
                      async_dispatcher_t* dispatcher) {
  framework_intermediary_proxy_.Bind(std::move(handle), dispatcher);
  framework_intermediary_proxy_.events().OnMockRunRequest =
      [=](std::string mock_id, fuchsia::realm::builder::MockComponentStartInfo start_info) {
        ZX_ASSERT_MSG(Contains(mock_id), "FrameworkIntemediary sent invalid mock id: %s",
                      mock_id.c_str());
        auto* mock = mocks_[mock_id];
        auto mock_handles = CreateFromStartInfo(std::move(start_info), dispatcher);
        mock->Start(std::move(mock_handles));
      };

  framework_intermediary_proxy_.events().OnMockStopRequest = [=](std::string mock_id) {
    ZX_ASSERT_MSG(Contains(mock_id), "FrameworkIntemediary sent invalid mock id: %s",
                  mock_id.c_str());
    mocks_.erase(mock_id);
  };
}

bool MockRunner::Contains(std::string mock_id) const {
  return mocks_.find(mock_id) != mocks_.cend();
}

}  // namespace sys::testing::internal
