// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/util/print_event.h"

#include "lib/fxl/logging.h"

using fuchsia::ui::gfx::Event;
using fuchsia::ui::gfx::MetricsEvent;
using fuchsia::ui::gfx::ImportUnboundEvent;
using fuchsia::ui::gfx::ViewDisconnectedEvent;
using fuchsia::ui::gfx::ViewAddedToSceneEvent;
using fuchsia::ui::gfx::ViewRemovedFromSceneEvent;
using fuchsia::ui::gfx::ViewPropertiesChangedEvent;

std::ostream& operator<<(std::ostream& stream,
                         const fuchsia::ui::gfx::Event& event) {
  switch (event.Which()) {
    case Event::Tag::kMetrics:
      return stream << event.metrics();
    case Event::Tag::kImportUnbound:
      return stream << event.import_unbound();
    case Event::Tag::kViewDisconnected:
      return stream << event.view_disconnected();
    case Event::Tag::kViewHolderDisconnected:
      return stream << event.view_holder_disconnected();
    case Event::Tag::kViewAddedToScene:
      return stream << event.view_added_to_scene();
    case Event::Tag::kViewRemovedFromScene:
      return stream << event.view_removed_from_scene();
    case Event::Tag::kViewPropertiesChanged:
      return stream << event.view_properties_changed();
    case Event::Tag::Invalid:
      return stream << "Invalid";
  }
}

std::ostream& operator<<(std::ostream& stream,
                         const fuchsia::ui::gfx::MetricsEvent& event) {
  return stream << "MetricsEvent(node_id=" << event.node_id
                << ", metrics=<TBD>)";
}

std::ostream& operator<<(std::ostream& stream,
                         const fuchsia::ui::gfx::ImportUnboundEvent& event){
  return stream << "ImportUnboundEvent(resource_id=" << event.resource_id
                << ")";
}

std::ostream& operator<<(std::ostream& stream,
                         const fuchsia::ui::gfx::ViewDisconnectedEvent& event) {
  return stream << "ViewDisconnectedEvent(view_id=" << event.view_id << ")";
}

std::ostream& operator<<(
    std::ostream& stream,
    const fuchsia::ui::gfx::ViewHolderDisconnectedEvent& event) {
  return stream << "ViewHolderDisconnectedEvent(view_holder_id="
                << event.view_holder_id << ")";
}

std::ostream& operator<<(std::ostream& stream,
                         const fuchsia::ui::gfx::ViewAddedToSceneEvent& event) {
  return stream << "ViewAddedToSceneEvent(view_id=" << event.view_id
                << ", properties=<TBD>";
}

std::ostream& operator<<(
    std::ostream& stream,
    const fuchsia::ui::gfx::ViewRemovedFromSceneEvent& event) {
  return stream << "ViewRemovedFromSceneEvent(view_id=" << event.view_id << ")";
}

std::ostream& operator<<(
    std::ostream& stream,
    const fuchsia::ui::gfx::ViewPropertiesChangedEvent& event) {
  return stream << "ViewPropertiesChangedEvent(view_id=" << event.view_id
                << ", properties=<TBD>)";
}

