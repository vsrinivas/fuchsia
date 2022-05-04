// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTING_UTIL_TEST_VIEW_H_
#define SRC_UI_TESTING_UTIL_TEST_VIEW_H_

#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <optional>

namespace ui_testing {

class TestView : public fuchsia::ui::app::ViewProvider, public component_testing::LocalComponent {
 public:
  enum class ContentType {
    // Draws an invisible rect in the view to give it "renderable" content.
    DEFAULT = 0,

    // Draws the following coordinate test pattern in a view:
    //
    // ___________________________________
    // |                |                |
    // |     BLACK      |        RED     |
    // |           _____|_____           |
    // |___________|  GREEN  |___________|
    // |           |_________|           |
    // |                |                |
    // |      BLUE      |     MAGENTA    |
    // |________________|________________|
    COORDINATE_GRID = 1,
  };

  explicit TestView(async_dispatcher_t* dispatcher, ContentType content_type)
      : dispatcher_(dispatcher), content_type_(content_type) {}
  ~TestView() override = default;

  // |component_testing::LocalComponent|
  void Start(std::unique_ptr<component_testing::LocalComponentHandles> mock_handles) override;

  // |fuchsia::ui::app::ViewProvider|
  void CreateViewWithViewRef(zx::eventpair token,
                             fuchsia::ui::views::ViewRefControl view_ref_control,
                             fuchsia::ui::views::ViewRef view_ref) override;

  // |fuchsia::ui::app::ViewProvider|
  void CreateView(zx::eventpair view_handle, fidl::InterfaceRequest<fuchsia::sys::ServiceProvider>,
                  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider>) override;

  // |fuchsia.ui.app.ViewProvider|
  void CreateView2(fuchsia::ui::app::CreateView2Args args) override;

  const std::optional<fuchsia::ui::views::ViewRef>& view_ref() { return view_ref_; }
  std::optional<zx_koid_t> GetViewRefKoid();

  float width();
  float height();

 private:
  // Helper methods to add content to the view.
  void DrawSimpleBackground();
  void DrawCoordinateGrid();
  void DrawContent();

  async_dispatcher_t* dispatcher_ = nullptr;
  std::optional<ContentType> content_type_;
  std::unique_ptr<component_testing::LocalComponentHandles> mock_handles_;
  fidl::BindingSet<fuchsia::ui::app::ViewProvider> view_provider_bindings_;

  // Scenic session resources.
  fuchsia::ui::scenic::ScenicPtr scenic_;
  std::unique_ptr<scenic::Session> session_;

  std::optional<fuchsia::ui::views::ViewRef> view_ref_;
  std::unique_ptr<scenic::View> test_view_;
  std::optional<fuchsia::ui::gfx::ViewProperties> test_view_properties_ = std::nullopt;
};

}  // namespace ui_testing

#endif  // SRC_UI_TESTING_UTIL_TEST_VIEW_H_
