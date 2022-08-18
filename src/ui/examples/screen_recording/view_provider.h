// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_SCREEN_RECORDING_VIEW_PROVIDER_H_
#define SRC_UI_EXAMPLES_SCREEN_RECORDING_VIEW_PROVIDER_H_

#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/composition/internal/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include "src/lib/ui/flatland-frame-scheduling/src/simple_present.h"

namespace screen_recording_example {

using fuchsia::ui::composition::TransformId;

using BouncingSquare = struct {
  int32_t x;
  int32_t y;
  int32_t x_speed;
  int32_t y_speed;
  fuchsia::math::SizeU size;
};

class ViewProviderImpl : fuchsia::ui::app::ViewProvider {
 public:
  ViewProviderImpl(sys::ComponentContext* component_context);
  ~ViewProviderImpl() override;

  // |fuchsia::ui::app::ViewProvider|
  void CreateView(zx::eventpair view_token,
                  fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
                  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services) override;

  // |fuchsia::ui::app::ViewProvider|
  void CreateView2(fuchsia::ui::app::CreateView2Args args) override;

 private:
  void DrawSquare();
  void CheckHit();
  void PresentCallback();
  void ScreenCaptureCallback();
  void SetUpFlatland();
  fuchsia::ui::composition::ColorRgba RandomColor();

  fidl::BindingSet<fuchsia::ui::app::ViewProvider> bindings_;
  std::optional<fuchsia::ui::composition::LayoutInfo> layout_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
  std::unique_ptr<simple_present::FlatlandConnection> flatland_connection_;
  fuchsia::ui::composition::Flatland* flatland_;
  fuchsia::ui::composition::AllocatorSyncPtr flatland_allocator_;
  fuchsia::ui::composition::ParentViewportWatcherPtr parent_watcher_;
  std::unique_ptr<sys::ComponentContext> context_;

  fuchsia::ui::composition::internal::ScreenCapturePtr screen_capture_;
  fuchsia::sysmem::BufferCollectionInfo_2 sc_buffer_collection_info_;

  const TransformId kRootTransformId{.value = 1};
  const TransformId kChildTransformId1{.value = 2};
  const TransformId kChildTransformId2{.value = 3};
  const TransformId kBouncingSquareTransformId{.value = 4};

  fuchsia::ui::composition::ContentId kSquareRectId;

  uint32_t num_buffers_ = 3;
  // Release fences passed into Present() for each buffer. Indexed by buffer index.
  std::vector<zx::event> present_release_fences_;

  BouncingSquare bs_ = {0, 0, 10, 10, {40, 40}};

  uint32_t display_width_ = 0;
  uint32_t display_height_ = 0;
  uint32_t half_display_width_ = 0;
  uint32_t num_pixels_ = 0;
};

}  // namespace screen_recording_example

#endif  // SRC_UI_EXAMPLES_SCREEN_RECORDING_VIEW_PROVIDER_H_
