// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_ANNOTATION_MANAGER_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_ANNOTATION_MANAGER_H_

#include <fuchsia/ui/annotation/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fit/function.h>

#include <vector>

#include "src/lib/fxl/memory/ref_counted.h"
#include "src/ui/scenic/lib/gfx/engine/scene_graph.h"
#include "src/ui/scenic/lib/gfx/engine/session_context.h"
#include "src/ui/scenic/lib/gfx/resources/view_holder.h"

namespace scenic_impl {
namespace gfx {

using OnAnnotationViewHolderCreatedCallback = fit::function<void()>;
using AnnotationHandlerId = uint32_t;
constexpr ResourceId kSessionId = 0U;

// Gfx implementation for annotation ViewHolder creation.
//
// The FIDL server handles all Scenic annotation creation requests, and
// only the requests of creating gfx Anntoation ViewHolders will be
// dispatched to this class.
//
// All annotation ViewHolder creation requests will be queued until
// we update all the Sessions, where it is safe to write to the
// SceneGraph.
class AnnotationManager {
 public:
  struct CreationRequest {
    bool fulfilled = false;

    fuchsia::ui::views::ViewRef main_view;
    ViewHolderPtr annotation_view_holder;
    OnAnnotationViewHolderCreatedCallback callback;
  };

  struct HandlerState {
    std::list<CreationRequest> requests;
    fit::function<void(zx_status_t)> on_handler_removed;
  };

  AnnotationManager(SceneGraphWeakPtr scene_graph, ViewLinker* view_linker,
                    std::unique_ptr<Session> session);

  bool HasHandler(AnnotationHandlerId handler_id) const;

  bool RegisterHandler(AnnotationHandlerId handler_id,
                       fit::function<void(zx_status_t)> on_handler_removed);

  bool RemoveHandlerWithEpitaph(AnnotationHandlerId handler_id, zx_status_t epitaph);

  void RequestCreate(AnnotationHandlerId handler_id, fuchsia::ui::views::ViewRef main_view,
                     fuchsia::ui::views::ViewHolderToken view_holder_token,
                     OnAnnotationViewHolderCreatedCallback callback);

  void FulfillCreateRequests();

 private:
  // Create annotation ViewHolder immediately.
  ViewHolderPtr NewAnnotationViewHolder(fuchsia::ui::views::ViewHolderToken view_holder_token);

  void CleanupInvalidHandlerState(
      const std::vector<std::pair<AnnotationHandlerId, zx_status_t>>& invalid_handlers_info);

  void CleanupFulfilledRequests();

  SceneGraphWeakPtr scene_graph_;
  ViewLinker* view_linker_;
  std::unique_ptr<Session> session_;

  std::map<AnnotationHandlerId, HandlerState> handlers_state_;

  fxl::WeakPtrFactory<AnnotationManager> weak_factory_;  // must be last
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_ANNOTATION_MANAGER_H_
