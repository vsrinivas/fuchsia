// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scenic/event_reporter.h"

#include <lib/syslog/cpp/macros.h>

#include "src/ui/scenic/lib/scenic/util/print_event.h"

namespace scenic_impl {
namespace {

// Define a "no-op" event reporter so that we may always assume
// Session::event_reporter_ is never null.
class DefaultEventReporter : public EventReporter {
 public:
  DefaultEventReporter() : weak_factory_(this) {
    FX_LOGS(INFO) << "EventReporter not set up, events will be dropped. This may be intended "
                     "behavior for some Scenic clients.";
  }

  void EnqueueEvent(fuchsia::ui::gfx::Event) override {}         // nop
  void EnqueueEvent(fuchsia::ui::input::InputEvent) override {}  // nop
  void EnqueueEvent(fuchsia::ui::scenic::Command) override {}    // nop

  EventReporterWeakPtr GetWeakPtr() override { return weak_factory_.GetWeakPtr(); }

 private:
  fxl::WeakPtrFactory<DefaultEventReporter> weak_factory_;  // must be last
};

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
      break;
    default:
      FX_LOGS(ERROR) << "Unknown Scenic event.";
  }
}

const std::shared_ptr<EventReporter>& EventReporter::Default() {
  static const std::shared_ptr<EventReporter> kReporter = std::make_shared<DefaultEventReporter>();
  return kReporter;
}

}  // namespace scenic_impl
