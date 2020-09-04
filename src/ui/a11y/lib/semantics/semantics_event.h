// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_SEMANTICS_EVENT_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_SEMANTICS_EVENT_H_

#include <zircon/types.h>

#include <optional>

namespace a11y {

// Describes the type of a semantics event.
enum class SemanticsEventType { kUnknown = 0, kSemanticTreeUpdated = 1 };

// Describes a semantics event of which semantics consumers must be notified.
struct SemanticsEventInfo {
  // Event type.
  SemanticsEventType event_type;

  // View in which the event occurred.
  // If empty, the event is not attached to a particular view.
  std::optional<zx_koid_t> view_ref_koid;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_SEMANTICS_EVENT_H_
