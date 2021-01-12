// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_SEMANTICS_EVENT_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_SEMANTICS_EVENT_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <zircon/types.h>

#include <optional>

namespace a11y {

// Describes the type of a semantics event.
// This event is internal to accessibility services, and not yet part of the FIDL. The long term
// goal is to have all of them migrated to the FIDL.
enum class SemanticsEventType { kUnknown = 0, kSemanticTreeUpdated = 1 };

// Describes a semantics event of which semantics consumers must be notified.
struct SemanticsEventInfo {
  // Event type. This is the internal generated event, and is only filled when |semantic_event| is
  // not.
  SemanticsEventType event_type = SemanticsEventType::kUnknown;

  // A semantic event fired by a semantics provider. This is only filled when |event_type| is not.
  std::optional<fuchsia::accessibility::semantics::SemanticEvent> semantic_event;

  // View in which the event occurred.
  // If empty, the event is not attached to a particular view.
  std::optional<zx_koid_t> view_ref_koid;

  // Because this struct is not copiable, a method to clone is contents is offered here.
  SemanticsEventInfo Clone() const {
    SemanticsEventInfo clone;
    clone.event_type = this->event_type;
    if (this->semantic_event) {
      fuchsia::accessibility::semantics::SemanticEvent semantic_event_clone;
      this->semantic_event->Clone(&semantic_event_clone);
      clone.semantic_event = std::move(semantic_event_clone);
    }

    clone.view_ref_koid = this->view_ref_koid;
    return clone;
  }
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_SEMANTICS_EVENT_H_
