// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_EVENT_REPORTER_H_
#define GARNET_LIB_UI_SCENIC_EVENT_REPORTER_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>

namespace scenic {

// Interface for a class that submits events to the SessionListener.
class EventReporter {
 public:
  // Add an event to our queue, which will be scheduled to be flushed and sent
  // to the event reporter later.
  virtual void EnqueueEvent(fuchsia::ui::scenic::Event event) = 0;
};

}  // namespace scenic

#endif  // GARNET_LIB_UI_SCENIC_EVENT_REPORTER_H_
