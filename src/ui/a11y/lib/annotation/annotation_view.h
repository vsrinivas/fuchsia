
// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_ANNOTATION_ANNOTATION_VIEW_H_
#define SRC_UI_A11Y_LIB_ANNOTATION_ANNOTATION_VIEW_H_

#include <fuchsia/ui/annotation/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <memory>
#include <optional>

#include "src/ui/a11y/lib/view/view_manager.h"

namespace a11y {

// The AnnotationView class enables the fuchsia accessibility manager to draw annotations over
// client views.
class AnnotationView : public fuchsia::ui::scenic::SessionListener {
 public:
  // Stores state of annotation view.
  struct AnnotationViewState {
    // True after annotation view has been registered via the scenic annotation registry API.
    bool annotation_view_registered = false;

    // True after the annotation view's node tree has been set up.
    bool tree_initialized = false;

    // True if annotations are currently attached to client view, and false otherwise.
    bool view_content_attached = false;

    // Node id for currently annotated node, if any.
    std::optional<uint32_t> annotated_node_id = {};
  };

  explicit AnnotationView(sys::ComponentContext* component_context, a11y::ViewManager* view_manager,
                          zx_koid_t client_view_koid);

  ~AnnotationView() = default;

  // NOTE: Callers MUST call InitializeView() before calling HighlightNode().
  // Creates an annotation view in session private to this view class and a corresponding view
  // holder in scenic, and then initializes the view's node structure to allow callers to annotate
  // the corresonding view.
  void InitializeView(fuchsia::ui::views::ViewRef client_view_ref);

  // Draws a bounding box around the node with id node_id.
  void HighlightNode(uint32_t node_id);

  // Hides annotation view contents by detaching the subtree containing the annotations from the
  // view.
  void DetachViewContents();

  zx_koid_t koid() { return client_view_koid_; }

  // Width of the four rectangles that constitute the boundaries of the highlight.
  static constexpr float kHighlightEdgeThickness = 0.01f;

  // IDs for resources common to all annotation views.
  static constexpr uint32_t kAnnotationViewId = 1;
  static constexpr uint32_t kContentNodeId = 2;
  static constexpr uint32_t kHighlightMaterialId = 3;
  static constexpr uint32_t kHighlightLeftEdgeNodeId = 4;
  static constexpr uint32_t kHighlightRightEdgeNodeId = 5;
  static constexpr uint32_t kHighlightTopEdgeNodeId = 6;
  static constexpr uint32_t kHighlightBottomEdgeNodeId = 7;

 private:
  // Helper function to draw four rectangles corresponding to the top, bottom, left, and right edges
  // of a node's bounding box.
  void DrawHighlight(const fuchsia::accessibility::semantics::Node* node);

  // Draws a rectangle to represent one edge of a highlight bounding box.
  void DrawHighlightEdge(std::vector<fuchsia::ui::scenic::Command>* cmds, int parent_node_id,
                         float width, float height, float center_x, float center_y,
                         float elevation);

  // Creates a node to hold one of the four highlight rectangle edges.
  void CreateHighlightEdgeNode(std::vector<fuchsia::ui::scenic::Command>* cmds, int edge_node_id);

  // Helper function to build a list of commands to enqueue.
  static void PushCommand(std::vector<fuchsia::ui::scenic::Command>* cmds,
                          fuchsia::ui::gfx::Command cmd);

  // Scenic error handler.
  void OnScenicError(std::string error) override {}

  // Scenic event handler.
  void OnScenicEvent(std::vector<fuchsia::ui::scenic::Event> events) override;

  // Helper function to handle gfx events (e.g. switching or resizing view).
  void HandleGfxEvent(const fuchsia::ui::gfx::Event& event);

  // Stores state of annotation view
  AnnotationViewState state_;

  // View manager object used to retrieve relevant semantic node info.
  a11y::ViewManager* view_manager_;

  // KOID of the client view this annotation view is used to annotate.
  zx_koid_t client_view_koid_;

  // Scenic session listener.
  fidl::Binding<fuchsia::ui::scenic::SessionListener> session_listener_binding_;

  // Scenic session interface.
  fuchsia::ui::scenic::SessionPtr session_;

  // Interface between a11y manager and Scenic annotation registry to register annotation
  // viewholder with Scenic.
  fuchsia::ui::annotation::RegistryPtr annotation_registry_;

  uint32_t next_resource_id_ = 8;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_ANNOTATION_ANNOTATION_VIEW_H_
