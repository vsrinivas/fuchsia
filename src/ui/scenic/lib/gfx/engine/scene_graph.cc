// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/scene_graph.h"

#include <lib/fostr/fidl/fuchsia/ui/input/formatting.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <zircon/status.h>

#include <sstream>

#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/util/time.h"
#include "src/ui/scenic/lib/utils/helpers.h"

namespace scenic_impl::gfx {

using fuchsia::ui::views::Error;
using fuchsia::ui::views::ViewRef;

CompositorWeakPtr SceneGraph::GetCompositor(GlobalId compositor_id) const {
  for (const CompositorWeakPtr& compositor : compositors_) {
    if (compositor && compositor->global_id() == compositor_id) {
      return compositor;
    }
  }
  return Compositor::kNullWeakPtr;
}

SceneGraph::SceneGraph() : weak_factory_(this) {}

void SceneGraph::AddCompositor(const CompositorWeakPtr& compositor) {
  FX_DCHECK(compositor);
  compositors_.push_back(compositor);
}

void SceneGraph::RemoveCompositor(const CompositorWeakPtr& compositor) {
  FX_DCHECK(compositor);
  auto it =
      std::find_if(compositors_.begin(), compositors_.end(),
                   [compositor](const auto& c) -> bool { return c.get() == compositor.get(); });
  FX_DCHECK(it != compositors_.end());
  compositors_.erase(it);
}

void SceneGraph::InvalidateAnnotationViewHolder(zx_koid_t koid) {
  view_tree_.InvalidateAnnotationViewHolder(koid);
}

// To avoid unnecessary complexity or cost of maintaining state, view_tree_ modifications are
// destructive.  This operation must preserve any needed state before applying updates.
void SceneGraph::ProcessViewTreeUpdates(ViewTreeUpdates view_tree_updates) {
  // Process all updates.
  for (auto& update : view_tree_updates) {
    if (auto ptr = std::get_if<ViewTreeNewRefNode>(&update)) {
      view_tree_.NewRefNode(std::move(*ptr));
    } else if (const auto ptr = std::get_if<ViewTreeNewAttachNode>(&update)) {
      view_tree_.NewAttachNode(ptr->koid);
    } else if (const auto ptr = std::get_if<ViewTreeDeleteNode>(&update)) {
      view_tree_.DeleteNode(ptr->koid);
    } else if (const auto ptr = std::get_if<ViewTreeMakeGlobalRoot>(&update)) {
      view_tree_.MakeGlobalRoot(ptr->koid);
    } else if (const auto ptr = std::get_if<ViewTreeConnectToParent>(&update)) {
      view_tree_.ConnectToParent(ptr->child, ptr->parent);
    } else if (const auto ptr = std::get_if<ViewTreeDisconnectFromParent>(&update)) {
      view_tree_.DisconnectFromParent(ptr->koid);
    } else {
      FX_NOTREACHED() << "Encountered unknown type of view tree update; variant index is: "
                      << update.index();
    }
  }
}

void SceneGraph::OnNewFocusedView(const zx_koid_t old_focus, const zx_koid_t new_focus) {
  FX_DCHECK(old_focus != new_focus);

  const zx_time_t focus_time = dispatcher_clock_now();
  if (old_focus != ZX_KOID_INVALID) {
    fuchsia::ui::input::FocusEvent focus;
    focus.event_time = focus_time;
    focus.focused = false;

    if (view_tree_.EventReporterOf(old_focus)) {
      fuchsia::ui::input::InputEvent input;
      input.set_focus(std::move(focus));
      view_tree_.EventReporterOf(old_focus)->EnqueueEvent(std::move(input));
    } else {
      FX_VLOGS(1) << "Old focus event; could not enqueue. No reporter. Event was: " << focus;
    }
  }

  if (new_focus != ZX_KOID_INVALID) {
    fuchsia::ui::input::FocusEvent focus;
    focus.event_time = focus_time;
    focus.focused = true;

    if (view_tree_.EventReporterOf(new_focus)) {
      fuchsia::ui::input::InputEvent input;
      input.set_focus(std::move(focus));
      view_tree_.EventReporterOf(new_focus)->EnqueueEvent(std::move(input));
    } else {
      FX_VLOGS(1) << "New focus event; could not enqueue. No reporter. Event was: " << focus;
    }
  }
}

}  // namespace scenic_impl::gfx
