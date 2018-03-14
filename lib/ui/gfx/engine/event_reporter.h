// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_EVENT_REPORTER_H_
#define GARNET_LIB_UI_GFX_ENGINE_EVENT_REPORTER_H_

#include "lib/ui/gfx/fidl/events.fidl.h"

namespace scenic {
namespace gfx {

// Interface for a class that submits events to the SessionListener.
class EventReporter {
 public:
  // Flushes enqueued session events to the session listener as a batch.
  virtual void SendEvents(::f1dl::Array<scenic::EventPtr> buffered_events) = 0;
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_ENGINE_EVENT_REPORTER_H_
