// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTING_UTIL_GFX_TEST_VIEW_H_
#define SRC_UI_TESTING_UTIL_GFX_TEST_VIEW_H_

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <optional>

#include "src/ui/testing/util/test_view.h"

namespace ui_testing {

class GfxTestView : public TestView {
 public:
  explicit GfxTestView(async_dispatcher_t* dispatcher, ContentType content_type)
      : TestView(dispatcher, content_type) {}
  ~GfxTestView() override = default;

  // |fuchsia::ui::app::ViewProvider|
  void CreateViewWithViewRef(zx::eventpair token,
                             fuchsia::ui::views::ViewRefControl view_ref_control,
                             fuchsia::ui::views::ViewRef view_ref) override;

  uint32_t width() override;
  uint32_t height() override;

 private:
  void DrawRectangle(int32_t x, int32_t y, int32_t z, uint32_t width, uint32_t height, uint8_t red,
                     uint8_t green, uint8_t blue, uint8_t alpha) override;
  void PresentChanges() override;

  // Scenic session resources.
  fuchsia::ui::scenic::ScenicPtr scenic_;
  std::unique_ptr<scenic::Session> session_;

  std::optional<fuchsia::ui::views::ViewRef> view_ref_;
  std::unique_ptr<scenic::View> test_view_;
  std::unique_ptr<scenic::EntityNode> root_node_;
  std::optional<fuchsia::ui::gfx::ViewProperties> test_view_properties_ = std::nullopt;
};

}  // namespace ui_testing

#endif  // SRC_UI_TESTING_UTIL_GFX_TEST_VIEW_H_
