// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_VIEW_VIEW_MANAGER_H_
#define SRC_UI_A11Y_LIB_VIEW_VIEW_MANAGER_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/accessibility/virtualkeyboard/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/types.h>

#include <memory>

#include "src/ui/a11y/lib/input_injection/injector_manager.h"
#include "src/ui/a11y/lib/semantics/semantic_tree.h"
#include "src/ui/a11y/lib/semantics/semantic_tree_service.h"
#include "src/ui/a11y/lib/semantics/semantics_event_manager.h"
#include "src/ui/a11y/lib/semantics/semantics_source.h"
#include "src/ui/a11y/lib/semantics/typedefs.h"
#include "src/ui/a11y/lib/view/accessibility_view.h"
#include "src/ui/a11y/lib/view/flatland_accessibility_view.h"
#include "src/ui/a11y/lib/view/view_coordinate_converter.h"
#include "src/ui/a11y/lib/view/view_injector_factory.h"
#include "src/ui/a11y/lib/view/view_source.h"
#include "src/ui/a11y/lib/view/view_wrapper.h"
#include "src/ui/a11y/lib/virtual_keyboard/virtual_keyboard_manager.h"

namespace a11y {

// A manager to manage the information offered by views to accessibility.
//
// Semantic Providers connect to this service to start supplying semantic
// information for a particular View while Semantic Consumers query available
// semantic information managed by this manager.
class ViewManager : public fuchsia::accessibility::semantics::SemanticsManager,
                    public fuchsia::accessibility::virtualkeyboard::Registry,
                    public fuchsia::accessibility::virtualkeyboard::Listener,
                    public InjectorManagerInterface,
                    // TODO(fxbug.dev/109954): Remove.
                    public SemanticsSource,
                    public ViewSource,
                    public VirtualKeyboardManager {
 public:
  explicit ViewManager(std::unique_ptr<SemanticTreeServiceFactory> factory,
                       std::unique_ptr<ViewSemanticsFactory> view_semantics_factory,
                       std::unique_ptr<AnnotationViewFactoryInterface> annotation_view_factory,
                       std::unique_ptr<ViewInjectorFactoryInterface> view_injector_factory,
                       std::unique_ptr<SemanticsEventManager> semantics_event_manager,
                       std::shared_ptr<AccessibilityViewInterface> a11y_view,
                       sys::ComponentContext* context);
  ~ViewManager() override;

  // Function to Enable/Disable Semantics Manager.
  // When Semantics are disabled, all the semantic tree bindings are
  // closed, which deletes all the semantic tree data.
  void SetSemanticsEnabled(bool enabled);

  bool GetSemanticsEnabled() { return semantics_enabled_; }

  // Returns a handle to the semantics event manager so that listeners
  // can register.
  SemanticsEventManager* GetSemanticsEventManager() { return semantics_event_manager_.get(); }

  // |SemanticsSource|
  bool ViewHasSemantics(zx_koid_t view_ref_koid) override;

  // |SemanticsSource|
  const fuchsia::accessibility::semantics::Node* GetSemanticNode(zx_koid_t koid,
                                                                 uint32_t node_id) const override;

  // |SemanticsSource|
  const fuchsia::accessibility::semantics::Node* GetNextNode(
      zx_koid_t koid, uint32_t node_id, a11y::NodeFilter filter) const override;

  // |SemanticsSource|
  const fuchsia::accessibility::semantics::Node* GetNextNode(
      zx_koid_t koid, uint32_t node_id, a11y::NodeFilterWithParent filter) const override;

  // |SemanticsSource|
  const fuchsia::accessibility::semantics::Node* GetParentNode(zx_koid_t koid,
                                                               uint32_t node_id) const override;

  // |SemanticsSource|
  const fuchsia::accessibility::semantics::Node* GetPreviousNode(
      zx_koid_t koid, uint32_t node_id, a11y::NodeFilter filter) const override;

  // |SemanticsSource|
  const fuchsia::accessibility::semantics::Node* GetPreviousNode(
      zx_koid_t koid, uint32_t node_id, a11y::NodeFilterWithParent filter) const override;

  // |SemanticsSource|
  bool ViewHasVisibleVirtualkeyboard(zx_koid_t view_ref_koid) override;

  // |SemanticsSource|
  std::optional<zx_koid_t> GetViewWithVisibleVirtualkeyboard() override;

  // |SemanticsSource|
  void ExecuteHitTesting(
      zx_koid_t koid, fuchsia::math::PointF local_point,
      fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback) override;

  // |SemanticsSource|
  void PerformAccessibilityAction(
      zx_koid_t koid, uint32_t node_id, fuchsia::accessibility::semantics::Action action,
      fuchsia::accessibility::semantics::SemanticListener::OnAccessibilityActionRequestedCallback
          callback) override;

  // |SemanticsSource|
  std::optional<SemanticTransform> GetNodeToRootTransform(zx_koid_t koid,
                                                          uint32_t node_id) const override;

  // |InjectorManagerInterface|
  bool InjectEventIntoView(fuchsia::ui::input::InputEvent& event, zx_koid_t koid) override;

  // |InjectorManagerInterface|
  bool MarkViewReadyForInjection(zx_koid_t koid, bool ready) override;

  // |ViewSource|
  fxl::WeakPtr<ViewWrapper> GetViewWrapper(zx_koid_t view_ref_koid) override;

  // Returns a pointer to the a11y view.
  std::shared_ptr<AccessibilityViewInterface> a11y_view() { return a11y_view_; }

  std::shared_ptr<FlatlandAccessibilityView> flatland_a11y_view() {
    return std::static_pointer_cast<FlatlandAccessibilityView>(a11y_view_);
  }

  // Sets a View Coordinate Converter to be used by this class.
  void SetViewCoordinateConverter(
      std::unique_ptr<ViewCoordinateConverter> view_coordinate_converter) {
    view_coordinate_converter_ = std::move(view_coordinate_converter);
  }
  ViewCoordinateConverter* GetViewCoordinateConverterForTest() {
    return view_coordinate_converter_.get();
  }

 private:
  // Helper function to retrieve the semantic tree corresponding to |koid|.
  // Returns nullptr if no such tree is found.
  const fxl::WeakPtr<::a11y::SemanticTree> GetTreeByKoid(const zx_koid_t koid) const;

  // |fuchsia::accessibility::semantics::SemanticsManager|:
  void RegisterViewForSemantics(
      fuchsia::ui::views::ViewRef view_ref,
      fidl::InterfaceHandle<fuchsia::accessibility::semantics::SemanticListener> handle,
      fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree> semantic_tree_request)
      override;

  // |fuchsia::accessibility::virtualkeyboard::Registry|:
  void Register(
      fuchsia::ui::views::ViewRef view_ref, bool is_visible,
      fidl::InterfaceRequest<fuchsia::accessibility::virtualkeyboard::Listener> listener) override;

  // |fuchsia::accessibility::virtualkeyboard::Listener|:
  void OnVisibilityChanged(bool updated_visibility, OnVisibilityChangedCallback callback) override;

  // ViewSignalHandler is called when ViewRef peer is destroyed. It is
  // responsible for closing the channel and cleaning up the associated SemanticTree.
  void ViewSignalHandler(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                         const zx_packet_signal* signal);

  // |SemanticsSource|
  std::optional<fuchsia::ui::views::ViewRef> ViewRefClone(zx_koid_t view_ref_koid) override;

  std::unordered_map<zx_koid_t, std::unique_ptr<ViewWrapper>> view_wrapper_map_;

  // TODO(fxbug.dev/36199): Move wait functions inside ViewWrapper.
  std::unordered_map<
      zx_koid_t, std::unique_ptr<async::WaitMethod<ViewManager, &ViewManager::ViewSignalHandler>>>
      wait_map_;

  bool semantics_enabled_ = false;

  std::unique_ptr<SemanticTreeServiceFactory> factory_;

  std::unique_ptr<ViewSemanticsFactory> view_semantics_factory_;

  std::unique_ptr<AnnotationViewFactoryInterface> annotation_view_factory_;

  std::unique_ptr<ViewInjectorFactoryInterface> view_injector_factory_;

  std::unique_ptr<SemanticsEventManager> semantics_event_manager_;

  std::shared_ptr<AccessibilityViewInterface> a11y_view_;

  std::unique_ptr<ViewCoordinateConverter> view_coordinate_converter_;

  fidl::Binding<fuchsia::accessibility::virtualkeyboard::Listener>
      virtualkeyboard_listener_binding_;
  std::pair<zx_koid_t, bool> virtualkeyboard_visibility_;

  sys::ComponentContext* context_;
};
}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_VIEW_VIEW_MANAGER_H_
