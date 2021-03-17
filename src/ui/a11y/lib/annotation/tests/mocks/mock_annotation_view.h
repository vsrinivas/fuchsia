// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_ANNOTATION_TESTS_MOCKS_MOCK_ANNOTATION_VIEW_H_
#define SRC_UI_A11Y_LIB_ANNOTATION_TESTS_MOCKS_MOCK_ANNOTATION_VIEW_H_

#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <map>
#include <memory>
#include <optional>

#include "src/ui/a11y/lib/annotation/annotation_view.h"

namespace accessibility_test {

class MockAnnotationView : public a11y::AnnotationViewInterface {
 public:
  MockAnnotationView(ViewPropertiesChangedCallback view_properties_changed_callback,
                     ViewAttachedCallback view_attached_callback,
                     ViewDetachedCallback view_detached_callback);

  ~MockAnnotationView() override = default;

  // |AnnotationViewInterface|
  void InitializeView(fuchsia::ui::views::ViewRef client_view_ref) override;

  // |AnnotationViewInterface|
  void DrawHighlight(const fuchsia::ui::gfx::BoundingBox& bounding_box,
                     const std::array<float, 3>& scale_vector,
                     const std::array<float, 3>& translation_vector,
                     bool is_magnification_highlight) override;

  // |AnnotationViewInterface|
  void ClearAllAnnotations() override;
  void ClearFocusHighlights() override;
  void ClearMagnificationHighlights() override;

  void SimulateViewPropertyChange();
  void SimulateViewAttachment();
  void SimulateViewDetachment();

  bool IsInitialized();
  const std::optional<fuchsia::ui::gfx::BoundingBox>& GetCurrentFocusHighlight();
  const std::optional<std::array<float, 3>> GetFocusHighlightScaleVector();
  const std::optional<std::array<float, 3>> GetFocusHighlightTranslationVector();
  const std::optional<fuchsia::ui::gfx::BoundingBox>& GetCurrentMagnificationHighlight();
  const std::optional<std::array<float, 3>> GetMagnificationHighlightScaleVector();
  const std::optional<std::array<float, 3>> GetMagnificationHighlightTranslationVector();

 private:
  ViewPropertiesChangedCallback view_properties_changed_callback_;
  ViewAttachedCallback view_attached_callback_;
  ViewDetachedCallback view_detached_callback_;

  bool initialize_view_called_ = false;
  std::optional<fuchsia::ui::gfx::BoundingBox> current_focus_highlight_;
  std::optional<std::array<float, 3>> current_focus_highlight_scale_;
  std::optional<std::array<float, 3>> current_focus_highlight_translation_;

  std::optional<fuchsia::ui::gfx::BoundingBox> current_magnification_highlight_;
  std::optional<std::array<float, 3>> current_magnification_highlight_scale_;
  std::optional<std::array<float, 3>> current_magnification_highlight_translation_;
};

class MockAnnotationViewFactory : public a11y::AnnotationViewFactoryInterface {
 public:
  MockAnnotationViewFactory() = default;
  ~MockAnnotationViewFactory() override = default;

  std::unique_ptr<a11y::AnnotationViewInterface> CreateAndInitAnnotationView(
      fuchsia::ui::views::ViewRef client_view_ref, sys::ComponentContext* context,
      a11y::AnnotationViewInterface::ViewPropertiesChangedCallback view_properties_changed_callback,
      a11y::AnnotationViewInterface::ViewAttachedCallback view_attached_callback,
      a11y::AnnotationViewInterface::ViewDetachedCallback view_detached_callback) override;

  MockAnnotationView* GetAnnotationView(zx_koid_t koid);

 private:
  std::map<zx_koid_t, MockAnnotationView*> annotation_views_;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_ANNOTATION_TESTS_MOCKS_MOCK_ANNOTATION_VIEW_H_
