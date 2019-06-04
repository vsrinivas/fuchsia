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
#include <zircon/errors.h>

using fuchsia::ui::input::KeyboardEventPhase;
using fuchsia::ui::scenic::Event;
using fuchsia::ui::scenic::Scenic;
using fuchsia::ui::scenic::Session;
using fuchsia::ui::scenic::SessionListener;

FakeSession::FakeSession(fidl::InterfaceRequest<Session> request,
                         fidl::InterfaceHandle<SessionListener> listener)
    : binding_(this, std::move(request)) {
  listener_.Bind(std::move(listener));
}

void FakeSession::SendEvents(std::vector<Event> events) {
  if (listener_.is_bound()) {
    listener_->OnScenicEvent(std::move(events));
  }
}

void FakeSession::set_error_handler(
    fit::function<void(zx_status_t)> error_handler) {
  binding_.set_error_handler(std::move(error_handler));
}

void FakeSession::NotImplemented_(const std::string& name) {
  FXL_LOG(INFO) << "Unimplemented method '" << name << "' called.";
}

void FakeScenic::SendEvents(std::vector<Event> event) {
  if (session_.has_value()) {
    session_->SendEvents(std::move(event));
  }
}

void FakeScenic::SendKeyEvent(KeyboardEventHidUsage usage,
                              fuchsia::ui::input::KeyboardEventPhase phase) {
  Event event;
  auto& keyboard_event = event.input().keyboard();
  keyboard_event.device_id = 0;
  keyboard_event.phase = phase;
  keyboard_event.hid_usage = static_cast<uint16_t>(usage);
  keyboard_event.code_point = 0;
  std::vector<Event> events;
  events.push_back(std::move(event));
  SendEvents(std::move(events));
}

fidl::InterfaceRequestHandler<Scenic> FakeScenic::GetHandler() {
  return bindings_.GetHandler(this);
}

void FakeScenic::CreateSession(
    fidl::InterfaceRequest<Session> session_request,
    fidl::InterfaceHandle<SessionListener> listener) {
  // Ensure we don't already have a session open.
  if (session_.has_value()) {
    FXL_LOG(WARNING)
        << "Attempt to create a second session on FakeScenic was rejected.";
    session_request.Close(ZX_ERR_NO_RESOURCES);
    return;
  }

  // Create a new session.
  session_.emplace(std::move(session_request), std::move(listener));
  session_->set_error_handler(
      [this](zx_status_t /*error*/) { session_.reset(); });
}

void FakeScenic::NotImplemented_(const std::string& name) {
  FXL_LOG(INFO) << "Unimplemented method '" << name << "' called.";
}
