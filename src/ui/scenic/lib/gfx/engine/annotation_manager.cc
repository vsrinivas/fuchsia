// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "annotation_manager.h"

#include "src/ui/scenic/lib/gfx/engine/object_linker.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/engine/view_tree.h"
#include "src/ui/scenic/lib/gfx/resources/view.h"
#include "src/ui/scenic/lib/gfx/resources/view_holder.h"
#include "src/ui/scenic/lib/utils/helpers.h"

namespace scenic_impl {
namespace gfx {

using fuchsia::ui::views::ViewHolderToken;
using fuchsia::ui::views::ViewRef;

AnnotationManager::AnnotationManager(SceneGraphWeakPtr scene_graph, ViewLinker* view_linker,
                                     std::unique_ptr<Session> session)
    : scene_graph_(scene_graph),
      view_linker_(view_linker),
      session_(std::move(session)),
      weak_factory_(this) {
  FX_DCHECK(scene_graph_);
  FX_DCHECK(view_linker_);
  FX_DCHECK(session_);
}

ViewHolderPtr AnnotationManager::NewAnnotationViewHolder(ViewHolderToken view_holder_token) {
  std::ostringstream annotation_debug_name;
  annotation_debug_name << "Annotation ViewHolder: Token " << view_holder_token.value.get();
  ViewHolderPtr annotation_view_holder = fxl::MakeRefCounted<ViewHolder>(
      session_.get(), session_->id(), /* node_id */ 0U, /* suppress_events */ true,
      annotation_debug_name.str(), session_->shared_error_reporter(),
      session_->view_tree_updater());

  // Set hit test behavior to kSuppress so it will suppress all hit testings.
  annotation_view_holder->SetHitTestBehavior(fuchsia::ui::gfx::HitTestBehavior::kSuppress);

  // Set up link with annotation View.
  ViewLinker::ExportLink link = view_linker_->CreateExport(
      annotation_view_holder.get(), std::move(view_holder_token.value), session_->error_reporter());
  FX_CHECK(link.valid()) << "Cannot setup link with annotation View!";
  annotation_view_holder->Connect(std::move(link));
  return annotation_view_holder;
}

bool AnnotationManager::HasHandler(AnnotationHandlerId handler_id) const {
  return handlers_state_.find(handler_id) != handlers_state_.end();
}

bool AnnotationManager::RegisterHandler(AnnotationHandlerId handler_id,
                                        fit::function<void(zx_status_t)> on_handler_removed) {
  if (HasHandler(handler_id)) {
    return false;
  }
  handlers_state_[handler_id] =
      HandlerState{.requests = {}, .on_handler_removed = std::move(on_handler_removed)};
  return true;
}

bool AnnotationManager::RemoveHandlerWithEpitaph(AnnotationHandlerId handler_id,
                                                 zx_status_t epitaph) {
  if (!HasHandler(handler_id)) {
    return false;
  }
  handlers_state_[handler_id].on_handler_removed(epitaph);
  handlers_state_.erase(handler_id);
  return true;
}

void AnnotationManager::RequestCreate(AnnotationHandlerId handler_id, ViewRef main_view,
                                      ViewHolderToken view_holder_token,
                                      OnAnnotationViewHolderCreatedCallback callback) {
  FX_CHECK(HasHandler(handler_id)) << "Handler ID " << handler_id << " invalid!";
  handlers_state_[handler_id].requests.push_back(CreationRequest{
      .fulfilled = false,
      .main_view = std::move(main_view),
      .annotation_view_holder = NewAnnotationViewHolder(std::move(view_holder_token)),
      .callback = std::move(callback),
  });
}

void AnnotationManager::CleanupInvalidHandlerState(
    const std::vector<std::pair<AnnotationHandlerId, zx_status_t>>& invalid_handlers_info) {
  std::unordered_set<AnnotationHandlerId> removed_handlers;
  // Clean up invalid handlers.
  for (const auto [handler_id, epitaph] : invalid_handlers_info) {
    if (removed_handlers.find(handler_id) == removed_handlers.end()) {
      auto result = RemoveHandlerWithEpitaph(handler_id, epitaph);
      removed_handlers.insert(handler_id);
      FX_DCHECK(result) << "Remove annotation handler #" << handler_id
                        << " failed: Handler doesn't exist.";
    }
  }
}

void AnnotationManager::CleanupFulfilledRequests() {
  for (auto& kv : handlers_state_) {
    HandlerState& state = kv.second;
    state.requests.remove_if([](const CreationRequest& request) { return request.fulfilled; });
  }
}

void AnnotationManager::FulfillCreateRequests() {
  std::vector<std::pair<AnnotationHandlerId, zx_status_t>> invalid_handlers_info;
  for (auto& kv : handlers_state_) {
    AnnotationHandlerId handler_id = kv.first;
    HandlerState& state = kv.second;

    for (auto& request : state.requests) {
      zx_koid_t main_view_koid = utils::ExtractKoid(request.main_view);
      zx_status_t status = scene_graph_->view_tree().AddAnnotationViewHolder(
          main_view_koid, request.annotation_view_holder);

      if (status == ZX_OK) {
        request.fulfilled = true;
        request.callback();
      } else if (status == ZX_ERR_PEER_CLOSED) {
        // This occurred when Session of |request.main_view| is destroyed
        // before AnnotationManager handles the request. In this case we only
        // need to remove it from the request list.
        request.fulfilled = true;
      } else if (status != ZX_ERR_NOT_FOUND) {
        invalid_handlers_info.emplace_back(handler_id, status);
        break;
      }
    }
  }
  CleanupInvalidHandlerState(invalid_handlers_info);
  CleanupFulfilledRequests();
}

void AnnotationManager::StageViewTreeUpdates() {
  session_->UpdateAndStageViewTreeUpdates(scene_graph_.get());
}

}  // namespace gfx
}  // namespace scenic_impl
