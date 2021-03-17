// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/annotation/tests/mocks/mock_annotation_view.h"

#include "src/ui/a11y/lib/util/util.h"

namespace accessibility_test {

MockAnnotationView::MockAnnotationView(
    ViewPropertiesChangedCallback view_properties_changed_callback,
    ViewAttachedCallback view_attached_callback, ViewDetachedCallback view_detached_callback)
    : view_properties_changed_callback_(std::move(view_properties_changed_callback)),
      view_attached_callback_(std::move(view_attached_callback)),
      view_detached_callback_(std::move(view_detached_callback)) {}

void MockAnnotationView::InitializeView(fuchsia::ui::views::ViewRef client_view_ref) {
  initialize_view_called_ = true;
}

void MockAnnotationView::DrawHighlight(const fuchsia::ui::gfx::BoundingBox& bounding_box,
                                       const std::array<float, 3>& scale_vector,
                                       const std::array<float, 3>& translation_vector,
                                       bool is_magnification_highlight) {
  if (is_magnification_highlight) {
    current_magnification_highlight_ =
        std::make_optional<fuchsia::ui::gfx::BoundingBox>(bounding_box);
    current_magnification_highlight_scale_ = std::make_optional<std::array<float, 3>>(scale_vector);
    current_magnification_highlight_translation_ =
        std::make_optional<std::array<float, 3>>(translation_vector);
  } else {
    current_focus_highlight_ = std::make_optional<fuchsia::ui::gfx::BoundingBox>(bounding_box);
    current_focus_highlight_scale_ = std::make_optional<std::array<float, 3>>(scale_vector);
    current_focus_highlight_translation_ =
        std::make_optional<std::array<float, 3>>(translation_vector);
  }
}

void MockAnnotationView::ClearAllAnnotations() {
  ClearFocusHighlights();
  ClearMagnificationHighlights();
}

void MockAnnotationView::ClearFocusHighlights() {
  current_focus_highlight_ = std::nullopt;
  current_focus_highlight_scale_ = std::nullopt;
  current_focus_highlight_translation_ = std::nullopt;
}

void MockAnnotationView::ClearMagnificationHighlights() {
  current_magnification_highlight_ = std::nullopt;
  current_magnification_highlight_scale_ = std::nullopt;
  current_magnification_highlight_translation_ = std::nullopt;
}

void MockAnnotationView::SimulateViewPropertyChange() { view_properties_changed_callback_(); }

void MockAnnotationView::SimulateViewAttachment() { view_attached_callback_(); }

void MockAnnotationView::SimulateViewDetachment() { view_detached_callback_(); }

bool MockAnnotationView::IsInitialized() { return initialize_view_called_; }

const std::optional<fuchsia::ui::gfx::BoundingBox>& MockAnnotationView::GetCurrentFocusHighlight() {
  return current_focus_highlight_;
}

const std::optional<std::array<float, 3>> MockAnnotationView::GetFocusHighlightScaleVector() {
  return current_focus_highlight_scale_;
}

const std::optional<std::array<float, 3>> MockAnnotationView::GetFocusHighlightTranslationVector() {
  return current_focus_highlight_translation_;
}

const std::optional<fuchsia::ui::gfx::BoundingBox>&
MockAnnotationView::GetCurrentMagnificationHighlight() {
  return current_magnification_highlight_;
}

const std::optional<std::array<float, 3>>
MockAnnotationView::GetMagnificationHighlightScaleVector() {
  return current_magnification_highlight_scale_;
}

const std::optional<std::array<float, 3>>
MockAnnotationView::GetMagnificationHighlightTranslationVector() {
  return current_magnification_highlight_translation_;
}

std::unique_ptr<a11y::AnnotationViewInterface>
MockAnnotationViewFactory::CreateAndInitAnnotationView(
    fuchsia::ui::views::ViewRef client_view_ref, sys::ComponentContext* context,
    a11y::AnnotationViewInterface::ViewPropertiesChangedCallback view_properties_changed_callback,
    a11y::AnnotationViewInterface::ViewAttachedCallback view_attached_callback,
    a11y::AnnotationViewInterface::ViewDetachedCallback view_detached_callback) {
  auto annotation_view = std::make_unique<MockAnnotationView>(
      std::move(view_properties_changed_callback), std::move(view_attached_callback),
      std::move(view_detached_callback));
  auto koid = a11y::GetKoid(client_view_ref);

  annotation_view->InitializeView(std::move(client_view_ref));

  annotation_views_[koid] = annotation_view.get();
  return annotation_view;
}

MockAnnotationView* MockAnnotationViewFactory::GetAnnotationView(zx_koid_t koid) {
  return annotation_views_[koid];
}

}  // namespace accessibility_test
