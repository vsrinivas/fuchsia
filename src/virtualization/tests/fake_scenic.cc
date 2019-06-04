// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/tests/fake_scenic.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/virtualization/hardware/cpp/fidl.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/defer.h>
#include <lib/svc/cpp/services.h>
#include <src/lib/fxl/logging.h>
#include <string.h>

void FakeSession::NotImplemented_(const std::string& name) {
  FXL_LOG(INFO) << "Unimplemented method '" << name << "' called.";
}

void FakeSession::Bind(
    fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session_request,
    fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener) {
  bindings_.AddBinding(this, std::move(session_request));
  listeners_.AddInterfacePtr(listener.Bind());
}

void FakeSession::BroadcastEvent(const fuchsia::ui::scenic::Event& event) {
  for (const auto& ptr : listeners_.ptrs()) {
    // Each call to OnScenicEvent consumes our Event object, so we must make
    // a new copy each loop iteration.
    fuchsia::ui::scenic::Event cloned_event;
    zx_status_t result = event.Clone(&cloned_event);
    FXL_CHECK(result == ZX_OK);
    std::vector<fuchsia::ui::scenic::Event> event_list;
    event_list.push_back(std::move(cloned_event));
    (*ptr)->OnScenicEvent(std::move(event_list));
  }
}

void FakeScenic::BroadcastEvent(const fuchsia::ui::scenic::Event& event) {
  session_.BroadcastEvent(event);
}

void FakeScenic::BroadcastKeyEvent(
    KeyboardEventHidUsage usage, fuchsia::ui::input::KeyboardEventPhase phase) {
  fuchsia::ui::scenic::Event event;
  auto& keyboard_event = event.input().keyboard();
  keyboard_event.device_id = 0;
  keyboard_event.phase = phase;
  keyboard_event.hid_usage = static_cast<uint16_t>(usage);
  keyboard_event.code_point = 0;
  BroadcastEvent(event);
}

fidl::InterfaceRequestHandler<fuchsia::ui::scenic::Scenic>
FakeScenic::GetHandler() {
  return bindings_.GetHandler(this);
}

void FakeScenic::CreateSession(
    fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session_request,
    fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener) {
  // We only have a single session, which will broadcast events to all
  // listeners.
  session_.Bind(std::move(session_request), std::move(listener));
}

void FakeScenic::NotImplemented_(const std::string& name) {
  FXL_LOG(INFO) << "Unimplemented method '" << name << "' called.";
}
