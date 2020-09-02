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

namespace scenic_impl {
namespace gfx {

using fuchsia::ui::focus::FocusChainListener;
using fuchsia::ui::focus::FocusChainListenerRegistry;
using fuchsia::ui::views::Error;
using fuchsia::ui::views::ViewRef;
using ViewFocuser = fuchsia::ui::views::Focuser;

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
  FX_DCHECK(app_context);
  if (app_context) {
    app_context->outgoing()->AddPublicService<FocusChainListenerRegistry>(
        [this](fidl::InterfaceRequest<FocusChainListenerRegistry> request) {
          focus_chain_listener_registry_.Bind(std::move(request));
        });
  } else {
    FX_LOGS(ERROR) << "SceneGraph failed to register fuchsia.ui.focus.FocusChainListenerRegistry.";
  }

  view_tree_.PublishViewRefInstalledService(app_context);
}

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
  view_tree_updates_.clear();
  view_tree_.PostProcessUpdates();

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
  const uint64_t id = next_focus_chain_listener_id_++;
  fuchsia::ui::focus::FocusChainListenerPtr new_listener;
  new_listener.Bind(std::move(focus_chain_listener));
  new_listener.set_error_handler([weak = weak_factory_.GetWeakPtr(), id](zx_status_t) {
    if (weak) {
      weak->focus_chain_listeners_.erase(id);
    }
  });
  auto [ignore, success] = focus_chain_listeners_.emplace(id, std::move(new_listener));
  FX_DCHECK(success);

  // Dispatch current chain on register.
  DispatchFocusChainTo(focus_chain_listeners_.at(id));
}

void SceneGraph::RegisterViewFocuser(SessionId session_id,
                                     fidl::InterfaceRequest<ViewFocuser> view_focuser) {
  FX_DCHECK(session_id != 0u) << "precondition";
  FX_DCHECK(view_focuser_endpoints_.count(session_id) == 0u) << "precondition";

  fit::function<void(ViewRef, ViewFocuser::RequestFocusCallback)> request_focus_handler =
      [this, session_id](ViewRef view_ref, ViewFocuser::RequestFocusCallback response) {
        bool is_honored = false;
        std::optional<zx_koid_t> requestor = this->view_tree().ConnectedViewRefKoidOf(session_id);
        if (requestor) {
          auto status = this->RequestFocusChange(requestor.value(), utils::ExtractKoid(view_ref));
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

void SceneGraph::DispatchFocusChainTo(const fuchsia::ui::focus::FocusChainListenerPtr& listener) {
  FocusChainListener::OnFocusChangeCallback callback = [] { /* No flow control yet. */ };
  listener->OnFocusChange(view_tree_.CloneFocusChain(), std::move(callback));
}

void SceneGraph::DispatchFocusChain() {
  TRACE_DURATION("gfx", "SceneGraphFocusChainDispatch", "chain_depth",
                 view_tree_.focus_chain().size());
  for (auto& [id, listener] : focus_chain_listeners_) {
    DispatchFocusChainTo(listener);
  }
}

void SceneGraph::MaybeDispatchFidlFocusChainAndFocusEvents(
    const std::vector<zx_koid_t>& old_focus_chain) {
  const std::vector<zx_koid_t>& new_focus_chain = view_tree_.focus_chain();

  if (old_focus_chain == new_focus_chain) {
    FX_VLOGS(1) << "Scenic, view focus changed: false" << std::endl
                << "\t Old focus chain: " << FocusChainToString(old_focus_chain);
    return;
  }

  FX_VLOGS(1) << "Scenic, view focus changed: true" << std::endl
              << "\t Old focus chain: " << FocusChainToString(old_focus_chain) << std::endl
              << "\t New focus chain: " << FocusChainToString(new_focus_chain);

  DispatchFocusChain();

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
      FX_VLOGS(1) << "Old focus event; could not enqueue. No reporter. Event was: " << focus;
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
      FX_VLOGS(1) << "New focus event; could not enqueue. No reporter. Event was: " << focus;
    }
  }
}

SceneGraph::ViewFocuserEndpoint::ViewFocuserEndpoint(
    fidl::InterfaceRequest<ViewFocuser> view_focuser,
    fit::function<void(ViewRef, RequestFocusCallback)> request_focus_handler)
    : request_focus_handler_(std::move(request_focus_handler)),
      endpoint_(this, std::move(view_focuser)) {
  FX_DCHECK(request_focus_handler_) << "invariant";
}

SceneGraph::ViewFocuserEndpoint::ViewFocuserEndpoint(ViewFocuserEndpoint&& original)
    : request_focus_handler_(std::move(original.request_focus_handler_)),
      endpoint_(this, original.endpoint_.Unbind()) {
  FX_DCHECK(request_focus_handler_) << "invariant";
}

void SceneGraph::ViewFocuserEndpoint::RequestFocus(ViewRef view_ref,
                                                   RequestFocusCallback response) {
  request_focus_handler_(std::move(view_ref), std::move(response));
}

}  // namespace gfx
}  // namespace scenic_impl
