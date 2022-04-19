// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTING_UI_TEST_MANAGER_GFX_TEST_VIEW_H_
#define SRC_UI_TESTING_UI_TEST_MANAGER_GFX_TEST_VIEW_H_

#include <fuchsia/ui/app/cpp/fidl.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <optional>

namespace ui_testing {

// For gfx test scenes, the ui test manager needs to insert its own view into
// the hierarchy to observe the state of the client view. This class implements
// the common logic required for the test manager to own this view.
class GfxTestView : public fuchsia::ui::app::ViewProvider {
 public:
  explicit GfxTestView(fuchsia::ui::scenic::ScenicPtr scenic)
      : view_provider_binding_(this), scenic_(std::move(scenic)) {}
  ~GfxTestView() override = default;

  // |fuchsia::ui::app::ViewProvider|
  void CreateViewWithViewRef(zx::eventpair token,
                             fuchsia::ui::views::ViewRefControl view_ref_control,
                             fuchsia::ui::views::ViewRef view_ref) override;

  // |fuchsia::ui::app::ViewProvider|
  void CreateView(zx::eventpair view_handle, fidl::InterfaceRequest<fuchsia::sys::ServiceProvider>,
                  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider>) override;

  // |fuchsia.ui.app.ViewProvider|
  void CreateView2(fuchsia::ui::app::CreateView2Args args) override;

  // Returns a fuchsia.ui.app.ViewProvider channel bound to this object.
  fidl::InterfaceHandle<fuchsia::ui::app::ViewProvider> NewViewProviderBinding();

  // Attaches a child view using the view provider specified.
  void AttachChildView(fuchsia::ui::app::ViewProviderPtr view_provider);

  const std::optional<fuchsia::ui::gfx::ViewProperties>& test_view_properties() const {
    return test_view_properties_;
  }
  const std::optional<fuchsia::ui::views::ViewRef>& child_view_ref() const {
    return child_view_ref_;
  }

  bool test_view_attached() const { return test_view_attached_; }
  bool child_view_connected() const { return child_view_connected_; }
  bool child_view_is_rendering() const { return child_view_is_rendering_; }

  float scale_factor() const { return scale_factor_; }

 private:
  fidl::Binding<fuchsia::ui::app::ViewProvider> view_provider_binding_;

  // Scenic session resources.
  fuchsia::ui::scenic::ScenicPtr scenic_;
  std::unique_ptr<scenic::Session> session_;

  std::unique_ptr<scenic::ViewHolder> child_view_holder_;
  std::unique_ptr<scenic::View> test_view_;
  std::optional<fuchsia::ui::views::ViewRef> child_view_ref_ = std::nullopt;

  std::optional<fuchsia::ui::gfx::ViewProperties> test_view_properties_ = std::nullopt;

  bool test_view_attached_ = false;
  bool child_view_connected_ = false;
  bool child_view_is_rendering_ = false;

  float scale_factor_ = 1.f;
};

}  // namespace ui_testing

#endif  // SRC_UI_TESTING_UI_TEST_MANAGER_GFX_TEST_VIEW_H_
