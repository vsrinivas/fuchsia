// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/scene_graph.h"

#include <lib/fostr/fidl/fuchsia/ui/input/formatting.h>
#include <zircon/status.h>

#include <sstream>

#include <trace/event.h>

#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/scene.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/traversal.h"
#include "src/ui/scenic/lib/gfx/resources/view.h"
#include "src/ui/scenic/lib/gfx/util/time.h"

namespace scenic_impl {
namespace gfx {

using fuchsia::ui::focus::FocusChainListener;
using fuchsia::ui::focus::FocusChainListenerRegistry;
using fuchsia::ui::views::Error;
using fuchsia::ui::views::ViewRef;
using ViewFocuser = fuchsia::ui::views::Focuser;

namespace {

//
// Finds descendant (including self) of type |View| by |koid|.
// Returns nullptr if matching View doesn't exist in the subtree of root.
//
// TODO(44170): ViewTree should contain the pointer to the View as well, so that
// we don't need to do traversal every time when we need a ViewPtr.
//
ViewPtr FindViewDescendantByViewKoid(Node* const root, zx_koid_t koid) {
  if (!root)
    return nullptr;

  if (root->IsKindOf<ViewNode>()) {
    ViewNodePtr view_node_ptr = root->As<ViewNode>();
    // FindOwningView() returns the View associated with the View node which
    // is stored in the node.
    ViewPtr view_ptr = view_node_ptr->FindOwningView();
    if (view_ptr && view_ptr->view_ref_koid() == koid) {
      return view_ptr;
    }
  }

  for (const NodePtr& child : root->children()) {
    ViewPtr child_result = FindViewDescendantByViewKoid(child.get(), koid);
    if (child_result) {
      return child_result;
    }
  }
  return nullptr;
}

}  // namespace

ViewPtr SceneGraph::LookupViewByViewRef(ViewRef view_ref) {
  zx_koid_t view_ref_koid = fsl::GetKoid(view_ref.reference.get());
  if (view_ref_koid == ZX_KOID_INVALID) {
    return nullptr;
  }

  std::set<Scene*> scenes;
  for (const auto& compositor : compositors()) {
    compositor->CollectScenes(&scenes);
  }

  for (Scene* scene : scenes) {
    ViewPtr scene_result = FindViewDescendantByViewKoid(scene, view_ref_koid);
    if (scene_result) {
      return scene_result;
    }
  }
  return nullptr;
}

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
    FXL_LOG(ERROR) << "SceneGraph failed to register fuchsia.ui.focus.FocusChainListenerRegistry.";
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
                            std::move(ptr->may_receive_focus), std::move(ptr->global_transform),
                            ptr->session_id);
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

void SceneGraph::Register(fidl::InterfaceHandle<FocusChainListener> focus_chain_listener) {
  focus_chain_listener_.Bind(std::move(focus_chain_listener));
}

void SceneGraph::RegisterViewFocuser(SessionId session_id,
                                     fidl::InterfaceRequest<ViewFocuser> view_focuser) {
  FXL_DCHECK(session_id != 0u) << "precondition";
  FXL_DCHECK(view_focuser_endpoints_.count(session_id) == 0u) << "precondition";

  fit::function<void(ViewRef, ViewFocuser::RequestFocusCallback)> request_focus_handler =
      [this, session_id](ViewRef view_ref, ViewFocuser::RequestFocusCallback response) {
        bool is_honored = false;
        std::optional<zx_koid_t> requestor = this->view_tree().ConnectedViewRefKoidOf(session_id);
        if (requestor) {
          auto status = this->RequestFocusChange(requestor.value(), ExtractKoid(view_ref));
          if (status == ViewTree::FocusChangeStatus::kAccept) {
            is_honored = true;
          }
        }

        if (is_honored) {
          response(fit::ok());  // Request received, and honored.
        } else {
          response(fit::error(Error::DENIED));  // Report a problem.
        }
      };

  view_focuser_endpoints_.emplace(
      session_id, ViewFocuserEndpoint(std::move(view_focuser), std::move(request_focus_handler)));
}

void SceneGraph::UnregisterViewFocuser(SessionId session_id) {
  view_focuser_endpoints_.erase(session_id);
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

SceneGraph::ViewFocuserEndpoint::ViewFocuserEndpoint(
    fidl::InterfaceRequest<ViewFocuser> view_focuser,
    fit::function<void(ViewRef, RequestFocusCallback)> request_focus_handler)
    : request_focus_handler_(std::move(request_focus_handler)),
      endpoint_(this, std::move(view_focuser)) {
  FXL_DCHECK(request_focus_handler_) << "invariant";
}

SceneGraph::ViewFocuserEndpoint::ViewFocuserEndpoint(ViewFocuserEndpoint&& original)
    : request_focus_handler_(std::move(original.request_focus_handler_)),
      endpoint_(this, original.endpoint_.Unbind()) {
  FXL_DCHECK(request_focus_handler_) << "invariant";
}

void SceneGraph::ViewFocuserEndpoint::RequestFocus(ViewRef view_ref,
                                                   RequestFocusCallback response) {
  request_focus_handler_(std::move(view_ref), std::move(response));
}

}  // namespace gfx
}  // namespace scenic_impl
