// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scenic/tests/scenic_test.h"

#include <lib/sys/cpp/testing/component_context_provider.h>

namespace scenic_impl {
namespace test {

void ScenicTest::SetUp() {
  sys::testing::ComponentContextProvider provider;
  context_ = provider.TakeContext();
  scenic_ =
      std::make_unique<Scenic>(context_.get(), inspect_deprecated::Node(), [this] { QuitLoop(); });
  InitializeScenic(scenic_.get());
}

void ScenicTest::TearDown() { scenic_.reset(); }

void ScenicTest::InitializeScenic(Scenic* scenic) {}

std::unique_ptr<::scenic::Session> ScenicTest::CreateSession() {
  fuchsia::ui::scenic::SessionPtr session_ptr;
  fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener_handle;
  fidl::InterfaceRequest<fuchsia::ui::scenic::SessionListener> listener_request =
      listener_handle.NewRequest();
  scenic()->CreateSession(session_ptr.NewRequest(), std::move(listener_handle));
  return std::make_unique<::scenic::Session>(std::move(session_ptr), std::move(listener_request));
}

}  // namespace test
}  // namespace scenic_impl
