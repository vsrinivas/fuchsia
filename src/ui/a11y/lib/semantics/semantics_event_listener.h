// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_SEMANTICS_EVENT_LISTENER_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_SEMANTICS_EVENT_LISTENER_H_

#include <zircon/types.h>

#include <optional>

#include "src/ui/a11y/lib/semantics/semantics_event.h"

namespace a11y {

// An interface for accessibility assistive technologies to listen for events on
// the semantic tree(s) from which they require up-to-date information.
class SemanticsEventListener {
 public:
  SemanticsEventListener() = default;
  virtual ~SemanticsEventListener() = default;

  // This method gets called on a semantics event.
  virtual void OnEvent(SemanticsEventInfo event_info) = 0;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_SEMANTICS_EVENT_LISTENER_H_
