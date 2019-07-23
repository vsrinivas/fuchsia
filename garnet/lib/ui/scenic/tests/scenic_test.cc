// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/displays/display.h"
#include "garnet/lib/ui/gfx/displays/display_manager.h"
#include "garnet/lib/ui/scenic/tests/scenic_gfx_test.h"

namespace scenic_impl {
namespace test {

std::unique_ptr<sys::ComponentContext> ScenicTest::app_context_;

void ScenicTest::SetUp() {
  // TODO(SCN-720): Wrap Create using ::gtest::Environment
  // instead of this hack.  This code has the chance to break non-ScenicTests.
  if (app_context_.get() == nullptr) {
    app_context_ = sys::ComponentContext::Create();
  }
  scenic_ = std::make_unique<Scenic>(app_context_.get(), inspect_deprecated::Node(),
                                     [this] { QuitLoop(); });
  InitializeScenic(scenic_.get());
}

void ScenicTest::TearDown() {
  events_.clear();
  scenic_.reset();
  app_context_.reset();
}

void ScenicTest::InitializeScenic(Scenic* scenic) {}

std::unique_ptr<::scenic::Session> ScenicTest::CreateSession() {
  fuchsia::ui::scenic::SessionPtr session_ptr;
  fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener_handle;
  fidl::InterfaceRequest<fuchsia::ui::scenic::SessionListener> listener_request =
      listener_handle.NewRequest();
  scenic()->CreateSession(session_ptr.NewRequest(), std::move(listener_handle));
  return std::make_unique<::scenic::Session>(std::move(session_ptr), std::move(listener_request));
}

void ScenicTest::EnqueueEvent(fuchsia::ui::gfx::Event event) {
  fuchsia::ui::scenic::Event scenic_event;
  scenic_event.set_gfx(std::move(event));
  events_.push_back(std::move(scenic_event));
}

void ScenicTest::EnqueueEvent(fuchsia::ui::input::InputEvent event) {
  fuchsia::ui::scenic::Event scenic_event;
  scenic_event.set_input(std::move(event));
  events_.push_back(std::move(scenic_event));
}

void ScenicTest::EnqueueEvent(fuchsia::ui::scenic::Command event) {
  fuchsia::ui::scenic::Event scenic_event;
  scenic_event.set_unhandled(std::move(event));
  events_.push_back(std::move(scenic_event));
}

}  // namespace test
}  // namespace scenic_impl
