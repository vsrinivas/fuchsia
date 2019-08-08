// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_EVENT_REPORTER_H_
#define GARNET_LIB_UI_SCENIC_EVENT_REPORTER_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>

#include "src/lib/fxl/memory/weak_ptr.h"

namespace scenic_impl {

class EventReporter;
using EventReporterWeakPtr = fxl::WeakPtr<EventReporter>;

// Interface for a class that submits events to the SessionListener.
class EventReporter {
 public:
  virtual ~EventReporter() = default;

  // Add a GFX event to our queue; schedule a flush by the event reporter.
  virtual void EnqueueEvent(fuchsia::ui::gfx::Event event) = 0;

  // Add an input event to our queue; immediate flush by the event reporter.
  virtual void EnqueueEvent(fuchsia::ui::input::InputEvent event) = 0;

  // Add an unhandled command event to our queue; schedule a flush.
  virtual void EnqueueEvent(fuchsia::ui::scenic::Command event) = 0;

  // Return a weak pointer to this object.
  virtual EventReporterWeakPtr GetWeakPtr() = 0;

  // Decode the event type and enqueue appropriately.
  void EnqueueEvent(fuchsia::ui::scenic::Event event);

  // A handy backup implementation. Logs an error and drops events.
  static const std::shared_ptr<EventReporter>& Default();
};

}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_SCENIC_EVENT_REPORTER_H_
