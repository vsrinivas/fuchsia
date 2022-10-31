// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/annotation/annotation_view.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

#include "src/ui/a11y/lib/util/util.h"

namespace a11y {

AnnotationView::AnnotationView(sys::ComponentContext* component_context,
                               ViewPropertiesChangedCallback view_properties_changed_callback,
                               ViewAttachedCallback view_attached_callback,
                               ViewDetachedCallback view_detached_callback)
    : session_listener_binding_(this),
      view_properties_changed_callback_(std::move(view_properties_changed_callback)),
      view_attached_callback_(std::move(view_attached_callback)),
      view_detached_callback_(std::move(view_detached_callback)) {
  fuchsia::ui::scenic::ScenicPtr scenic =
      component_context->svc()->Connect<fuchsia::ui::scenic::Scenic>();

  // Create a Scenic Session and a Scenic SessionListener.
  scenic->CreateSession(session_.NewRequest(), session_listener_binding_.NewBinding());

  // Connect to Scenic annotation registry service.
  annotation_registry_ = component_context->svc()->Connect<fuchsia::ui::annotation::Registry>();
  annotation_registry_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Error from fuchsia::ui::annotation::Registry: "
                   << zx_status_get_string(status);
  });
}

void AnnotationView::InitializeView(fuchsia::ui::views::ViewRef client_view_ref) {
  FX_CHECK(client_view_ref.reference);
  client_view_koid_ = GetKoid(client_view_ref);

  std::vector<fuchsia::ui::scenic::Command> cmds;

  // Create view token pair for annotation view and view holder.
  auto [annotation_view_token, annotation_view_holder_token] = scenic::ViewTokenPair::New();

  // Register annotation view holder with scenic.
  annotation_registry_->CreateAnnotationViewHolder(
      std::move(client_view_ref), std::move(annotation_view_holder_token),
      [this]() { state_.annotation_view_registered = true; });

  // Use |view_token| to create annotation view in the session.
  PushCommand(&cmds, scenic::NewCreateViewCmd(kAnnotationViewId, std::move(annotation_view_token),
                                              "annotation_view"));

  // Create entity node that will be the parent of all the annotation content. Attaching the
  // annotation content as children of this node enables us to clear the contents of the view by
  // detaching only this node from the view.
  PushCommand(&cmds, scenic::NewCreateEntityNodeCmd(kFocusHighlightContentNodeId));

  // Create material (fill color) for highlights. Chosen arbitrarily.
  PushCommand(&cmds, scenic::NewCreateMaterialCmd(kFocusHighlightMaterialId));
  PushCommand(&cmds, scenic::NewSetColorCmd(kFocusHighlightMaterialId, 0xf5, 0x00, 0x57, 0xff));

  // Create shape nodes to hold each of the edges of the highlight.
  CreateHighlightEdgeNode(&cmds, kFocusHighlightLeftEdgeNodeId);
  CreateHighlightEdgeNode(&cmds, kFocusHighlightRightEdgeNodeId);
  CreateHighlightEdgeNode(&cmds, kFocusHighlightTopEdgeNodeId);
  CreateHighlightEdgeNode(&cmds, kFocusHighlightBottomEdgeNodeId);

  // Enque commands to create view.
  session_->Enqueue(std::move(cmds));

  // Apply commands.
  session_->Present(0, {}, {}, [this](fuchsia::images::PresentationInfo info) {
    state_.tree_initialized = true;
  });
}

void AnnotationView::DrawHighlight(const fuchsia::ui::gfx::BoundingBox& bounding_box,
                                   const std::array<float, 3>& scale_vector,
                                   const std::array<float, 3>& translation_vector) {
  if (!state_.tree_initialized) {
    FX_LOGS(INFO) << "Annotation view tree is not initialized.";
    return;
  }

  if (!state_.annotation_view_registered) {
    FX_LOGS(INFO) << "Annotation view not registered.";
    return;
  }

  // Used to specify height/width of edge rectangles.
  const auto bounding_box_width = bounding_box.max.x - bounding_box.min.x;
  const auto bounding_box_height = bounding_box.max.y - bounding_box.min.y;

  // Annotation views currently have the same bounding boxes as their parent views, so in order
  // to ensure that annotations appear superimposed on parent view content, the elevation should
  // be set to the top of the parent view.

  // Add some offset to ensure these rectangles fall into the View bounding box
  // so that it's drawable.
  // TODO(fxbug.dev/64277): Ideally we would draw this higher in the scene graph so that we can't
  // accidentally draw behind the camera.  In the mean time, we draw 5% between min z and max z
  // in our view to give some extra space for z translations higher up in the scene graph.  It
  // is very common to find a flutter::LayerTree EntityNode that applies a -10 z translation,
  // and the bounding box min z and max z are typically -1000 and 1000, so make sure we have
  // plenty of space for that.
  constexpr float kEpsilon = 0.05;
  const auto annotation_elevation = parent_view_properties_.bounding_box.min.z * (1 - kEpsilon);

  // Used to translate edge rectangles.
  const auto bounding_box_center_x = (bounding_box.max.x + bounding_box.min.x) / 2.f;
  const auto bounding_box_center_y = (bounding_box.max.y + bounding_box.min.y) / 2.f;

  std::vector<fuchsia::ui::scenic::Command> cmds;

  uint32_t left_edge_id = kFocusHighlightLeftEdgeNodeId;
  uint32_t right_edge_id = kFocusHighlightRightEdgeNodeId;
  uint32_t top_edge_id = kFocusHighlightTopEdgeNodeId;
  uint32_t bottom_edge_id = kFocusHighlightBottomEdgeNodeId;

  // Create edges of highlight rectangle.
  DrawHighlightEdge(&cmds, left_edge_id, kHighlightEdgeThickness,
                    bounding_box_height + kHighlightEdgeThickness, bounding_box.min.x,
                    bounding_box_center_y, annotation_elevation);
  DrawHighlightEdge(&cmds, right_edge_id, kHighlightEdgeThickness,
                    bounding_box_height + kHighlightEdgeThickness, bounding_box.max.x,
                    bounding_box_center_y, annotation_elevation);
  DrawHighlightEdge(&cmds, top_edge_id, bounding_box_width + kHighlightEdgeThickness,
                    kHighlightEdgeThickness, bounding_box_center_x, bounding_box.max.y,
                    annotation_elevation);
  DrawHighlightEdge(&cmds, bottom_edge_id, bounding_box_width + kHighlightEdgeThickness,
                    kHighlightEdgeThickness, bounding_box_center_x, bounding_box.min.y,
                    annotation_elevation);

  // Attach the correct content node (if necessary) to the annotation view node
  // to render the annotation.
  if (!state_.view_content_attached) {
    PushCommand(&cmds,
                scenic::NewSetTranslationCmd(kFocusHighlightContentNodeId, translation_vector));
    PushCommand(&cmds, scenic::NewSetScaleCmd(kFocusHighlightContentNodeId, scale_vector));
    PushCommand(&cmds, scenic::NewAddChildCmd(kAnnotationViewId, kFocusHighlightContentNodeId));
    state_.view_content_attached = true;
  }

  session_->Enqueue(std::move(cmds));
  session_->Present(0, {}, {}, [](fuchsia::images::PresentationInfo info) {});
}

void AnnotationView::DetachViewContents(uint32_t node_to_detach) {
  std::vector<fuchsia::ui::scenic::Command> cmds;

  // Clear view contents by detaching top-level content node from view.
  PushCommand(&cmds, scenic::NewDetachCmd(node_to_detach));
  session_->Enqueue(std::move(cmds));
  session_->Present(0, {}, {}, [](fuchsia::images::PresentationInfo info) {});
}

void AnnotationView::ClearAllAnnotations() { ClearFocusHighlights(); }

void AnnotationView::ClearFocusHighlights() {
  DetachViewContents(kFocusHighlightContentNodeId);
  state_.view_content_attached = false;
}

void AnnotationView::DrawHighlightEdge(std::vector<fuchsia::ui::scenic::Command>* cmds,
                                       int parent_node_id, float width, float height,
                                       float center_x, float center_y, float elevation) {
  const auto edge_id = next_resource_id_++;

  PushCommand(cmds, scenic::NewCreateRectangleCmd(edge_id, width, height));
  PushCommand(cmds, scenic::NewSetShapeCmd(parent_node_id, edge_id));
  // By releasing the resource here, we make the edge's parent node the only holder of a reference
  // to the edge rectangle. Once the background shape node no longer references this rectangle,
  // scenic will destroy it internally. This behavior ensures that we don't need to explicitly
  // delete annotations when we want to create new ones -- we can simply call invoke NewSetShapeCmd
  // with the updated shape to delete the old one.
  PushCommand(cmds, scenic::NewReleaseResourceCmd(edge_id));
  PushCommand(cmds, scenic::NewSetTranslationCmd(parent_node_id, {center_x, center_y, elevation}));
}

void AnnotationView::PushCommand(std::vector<fuchsia::ui::scenic::Command>* cmds,
                                 fuchsia::ui::gfx::Command cmd) {
  // Wrap the gfx::Command in a scenic::Command, then push it.
  cmds->push_back(scenic::NewCommand(std::move(cmd)));
}

void AnnotationView::CreateHighlightEdgeNode(std::vector<fuchsia::ui::scenic::Command>* cmds,
                                             int edge_node_id) {
  PushCommand(cmds, scenic::NewCreateShapeNodeCmd(edge_node_id));
  PushCommand(cmds, scenic::NewSetMaterialCmd(edge_node_id, kFocusHighlightMaterialId));
  PushCommand(cmds, scenic::NewAddChildCmd(kFocusHighlightContentNodeId, edge_node_id));
}

void AnnotationView::OnScenicEvent(std::vector<fuchsia::ui::scenic::Event> events) {
  for (const auto& event : events) {
    if (event.Which() != fuchsia::ui::scenic::Event::Tag::kGfx) {
      // We don't expect to receive any input events, and can ignore unhandled events.
      continue;
    }
    HandleGfxEvent(event.gfx());
  }
}

void AnnotationView::HandleGfxEvent(const fuchsia::ui::gfx::Event& event) {
  if (event.Which() == fuchsia::ui::gfx::Event::Tag::kViewPropertiesChanged) {
    // Update parent View properties.
    parent_view_properties_ = event.view_properties_changed().properties;
    view_properties_changed_callback_();
  } else if (event.Which() == fuchsia::ui::gfx::Event::Tag::kViewDetachedFromScene) {
    view_detached_callback_();
  } else if (event.Which() == fuchsia::ui::gfx::Event::Tag::kViewAttachedToScene) {
    view_attached_callback_();
  }
}

std::unique_ptr<AnnotationViewInterface> AnnotationViewFactory::CreateAndInitAnnotationView(
    fuchsia::ui::views::ViewRef client_view_ref, sys::ComponentContext* context,
    AnnotationViewInterface::ViewPropertiesChangedCallback view_properties_changed_callback,
    AnnotationViewInterface::ViewAttachedCallback view_attached_callback,
    AnnotationViewInterface::ViewDetachedCallback view_detached_callback) {
  auto annotation_view = std::make_unique<AnnotationView>(
      context, std::move(view_properties_changed_callback), std::move(view_attached_callback),
      std::move(view_detached_callback));

  annotation_view->InitializeView(std::move(client_view_ref));

  return annotation_view;
}

}  // namespace a11y
