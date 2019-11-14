// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/scene_graph.h"

#include <lib/fostr/fidl/fuchsia/ui/input/formatting.h>

#include <sstream>

#include <trace/event.h>

#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/util/time.h"

namespace scenic_impl {
namespace gfx {

using fuchsia::ui::focus::FocusChainListener;
using fuchsia::ui::focus::FocusChainListenerRegistry;

CompositorWeakPtr SceneGraph::GetCompositor(GlobalId compositor_id) const {
  for (const CompositorWeakPtr& compositor : compositors_) {
    if (compositor && compositor->global_id() == compositor_id) {
      return compositor;
    }
  }
  return Compositor::kNullWeakPtr;
}

SceneGraph::SceneGraph(sys::ComponentContext* app_context)
    : focus_chain_listener_registry_(this), weak_factory_(this) {
  FXL_DCHECK(app_context);
  if (app_context) {
    app_context->outgoing()->AddPublicService<FocusChainListenerRegistry>(
        [this](fidl::InterfaceRequest<FocusChainListenerRegistry> request) {
          focus_chain_listener_registry_.Bind(std::move(request));
        });

  } else {
    FXL_LOG(ERROR) << "SceneGraph failed to register FocusChainListenerRegistry.";
  }
}

void SceneGraph::AddCompositor(const CompositorWeakPtr& compositor) {
  FXL_DCHECK(compositor);
  compositors_.push_back(compositor);
}

void SceneGraph::RemoveCompositor(const CompositorWeakPtr& compositor) {
  FXL_DCHECK(compositor);
  auto it =
      std::find_if(compositors_.begin(), compositors_.end(),
                   [compositor](const auto& c) -> bool { return c.get() == compositor.get(); });
  FXL_DCHECK(it != compositors_.end());
  compositors_.erase(it);
}

void SceneGraph::StageViewTreeUpdates(ViewTreeUpdates updates) {
  for (auto& update : updates) {
    view_tree_updates_.push_back(std::move(update));
  }
}

// To avoid unnecessary complexity or cost of maintaining state, view_tree_ modifications are
// destructive.  This operation must preserve any needed state before applying updates.
void SceneGraph::ProcessViewTreeUpdates() {
  std::vector<zx_koid_t> old_focus_chain = view_tree_.focus_chain();

  // Process all updates.
  for (auto& update : view_tree_updates_) {
    if (auto ptr = std::get_if<ViewTreeNewRefNode>(&update)) {
      view_tree_.NewRefNode(std::move(ptr->view_ref), ptr->event_reporter,
                            std::move(ptr->may_receive_focus), ptr->gfx_session_id);
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
      FXL_NOTREACHED() << "Encountered unknown type of view tree update; variant index is: "
                       << update.index();
    }
  }
  view_tree_updates_.clear();

  MaybeDispatchFidlFocusChainAndFocusEvents(old_focus_chain);
}

ViewTree::FocusChangeStatus SceneGraph::RequestFocusChange(zx_koid_t requestor, zx_koid_t request) {
  std::vector<zx_koid_t> old_focus_chain = view_tree_.focus_chain();

  ViewTree::FocusChangeStatus status = view_tree_.RequestFocusChange(requestor, request);
  if (status == ViewTree::FocusChangeStatus::kAccept) {
    MaybeDispatchFidlFocusChainAndFocusEvents(old_focus_chain);
  }
  return status;
}

void SceneGraph::Register(
    fidl::InterfaceHandle<fuchsia::ui::focus::FocusChainListener> focus_chain_listener) {
  focus_chain_listener_.Bind(std::move(focus_chain_listener));
}

std::string FocusChainToString(const std::vector<zx_koid_t>& focus_chain) {
  if (focus_chain.empty())
    return "(none)";

  std::stringstream output;
  output << "[";
  for (zx_koid_t koid : focus_chain) {
    output << koid << " ";
  }
  output << "]";
  return output.str();
}

void SceneGraph::MaybeDispatchFidlFocusChainAndFocusEvents(
    const std::vector<zx_koid_t>& old_focus_chain) {
  const std::vector<zx_koid_t>& new_focus_chain = view_tree_.focus_chain();

  bool focus_changed = (old_focus_chain != new_focus_chain);

  FXL_VLOG(1) << "Scenic, view focus changed: " << std::boolalpha << focus_changed << std::endl
              << "\t Old focus chain: " << FocusChainToString(old_focus_chain) << std::endl
              << "\t New focus chain: " << FocusChainToString(new_focus_chain);

  if (focus_changed) {
    if (focus_chain_listener_) {
      TRACE_DURATION("gfx", "SceneGraphFocusChainDispatch", "chain_depth", new_focus_chain.size());
      FocusChainListener::OnFocusChangeCallback callback = [] { /* No flow control yet. */ };
      focus_chain_listener_->OnFocusChange(view_tree_.CloneFocusChain(), std::move(callback));
    }

    const zx_time_t focus_time = dispatcher_clock_now();
    if (!old_focus_chain.empty()) {
      fuchsia::ui::input::FocusEvent focus;
      focus.event_time = focus_time;
      focus.focused = false;

      if (view_tree_.EventReporterOf(old_focus_chain.back())) {
        fuchsia::ui::input::InputEvent input;
        input.set_focus(std::move(focus));
        view_tree_.EventReporterOf(old_focus_chain.back())->EnqueueEvent(std::move(input));
      } else {
        FXL_VLOG(1) << "Old focus event; could not enqueue. No reporter. Event was: " << focus;
      }
    }

    if (!new_focus_chain.empty()) {
      fuchsia::ui::input::FocusEvent focus;
      focus.event_time = focus_time;
      focus.focused = true;

      if (view_tree_.EventReporterOf(new_focus_chain.back())) {
        fuchsia::ui::input::InputEvent input;
        input.set_focus(std::move(focus));
        view_tree_.EventReporterOf(new_focus_chain.back())->EnqueueEvent(std::move(input));
      } else {
        FXL_VLOG(1) << "New focus event; could not enqueue. No reporter. Event was: " << focus;
      }
    }
  }
}

}  // namespace gfx
}  // namespace scenic_impl
