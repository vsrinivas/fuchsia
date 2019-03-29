// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/event_reporter.h"

#include "garnet/lib/ui/scenic/util/print_event.h"
#include "lib/fostr/fidl/fuchsia/ui/scenic/formatting.h"
#include "src/lib/fxl/logging.h"
#include "lib/ui/input/cpp/formatting.h"

namespace scenic_impl {
namespace {

// Define a "no-op" event reporter so that we may always assume
// Session::event_reporter_ is never null.
class IdleEventReporter : public EventReporter {
 public:
  constexpr IdleEventReporter() = default;
  ~IdleEventReporter() = default;  // Trivially destructible.

  void EnqueueEvent(fuchsia::ui::gfx::Event event) override {
    FXL_LOG(WARNING) << "EventReporter not set up, dropped event: " << event;
  }

  void EnqueueEvent(fuchsia::ui::input::InputEvent event) override {
    FXL_LOG(WARNING) << "EventReporter not set up, dropped event: " << event;
  }

  void EnqueueEvent(fuchsia::ui::scenic::Command unhandled) override {
    FXL_LOG(WARNING) << "EventReporter not set up, dropped event: "
                     << unhandled;
  }
};

IdleEventReporter* GetIdleEventReporter() {
  // Global but safe.
  static IdleEventReporter idle_event_reporter;
  return &idle_event_reporter;
}

}  // namespace

void EventReporter::EnqueueEvent(fuchsia::ui::scenic::Event event) {
  switch (event.Which()) {
    case fuchsia::ui::scenic::Event::Tag::kGfx:
      EnqueueEvent(std::move(event.gfx()));
      break;
    case fuchsia::ui::scenic::Event::Tag::kInput:
      EnqueueEvent(std::move(event.input()));
      break;
    case fuchsia::ui::scenic::Event::Tag::kUnhandled:
      EnqueueEvent(std::move(event.unhandled()));
    default:
      FXL_LOG(ERROR) << "Unknown Scenic event.";
  }
}

EventReporter* EventReporter::Default() { return GetIdleEventReporter(); }

}  // namespace scenic_impl
