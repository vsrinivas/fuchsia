// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTING_UTIL_FLATLAND_TEST_VIEW_H_
#define SRC_UI_TESTING_UTIL_FLATLAND_TEST_VIEW_H_

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>

#include <optional>

#include "fuchsia/ui/app/cpp/fidl.h"
#include "src/ui/testing/util/test_view.h"

namespace ui_testing {

class FlatlandTestView : public TestView {
 public:
  explicit FlatlandTestView(async_dispatcher_t* dispatcher, ContentType content_type)
      : TestView(dispatcher, content_type) {}
  ~FlatlandTestView() override = default;

  // |fuchsia::ui::app::ViewProvider|
  void CreateView2(fuchsia::ui::app::CreateView2Args args) override;

  // Add a child view!
  // The viewport will have side length of 1/4 our side length and will be centered in our view.
  void NestChildView();

  uint32_t width() override;
  uint32_t height() override;

 private:
  void DrawRectangle(int32_t x, int32_t y, int32_t z, uint32_t width, uint32_t height, uint8_t red,
                     uint8_t green, uint8_t blue, uint8_t alpha) override;
  void PresentChanges() override;
  void ResizeChildViewport();

  // Scene graph:
  // root transform (id=1)
  // --> rectangle holder transform (id=2)
  //     --> ... (optional) rectangles (id=100, 101, 102, ...)
  // --> (optional) child viewport transform (id=3) {content: child viewport id=4}
  const uint64_t kRootTransformId = 1;
  const uint64_t kRectangleHolderTransform = 2;
  const uint64_t kChildViewportTransformId = 3;
  const uint64_t kChildViewportContentId = 4;

  // We'll keep incrementing this to get the next resource id (100, 101, 102, ...)
  uint64_t next_resource_id_ = 100;

  bool child_view_is_nested = false;

  // Scenic session resources.
  fuchsia::ui::composition::FlatlandPtr flatland_;

  // Used to retrieve a11y view layout info. These should not change over the
  // lifetime of the view.
  fuchsia::ui::composition::ParentViewportWatcherPtr parent_watcher_;

  std::optional<fuchsia::ui::composition::LayoutInfo> layout_info_;
};

}  // namespace ui_testing

#endif  // SRC_UI_TESTING_UTIL_FLATLAND_TEST_VIEW_H_
