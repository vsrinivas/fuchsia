// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_MOZART_EVENT_REPORTER_H_
#define GARNET_LIB_UI_MOZART_EVENT_REPORTER_H_

#include "lib/ui/mozart/fidl/events.fidl.h"

namespace mz {

// Interface for a class that submits events to the SessionListener.
class EventReporter {
 public:
  // Flushes enqueued session events to the session listener as a batch.
  virtual void SendEvents(
      ::f1dl::Array<ui_mozart::EventPtr> buffered_events) = 0;
};

}  // namespace mz

#endif  // GARNET_LIB_UI_MOZART_EVENT_REPORTER_H_
