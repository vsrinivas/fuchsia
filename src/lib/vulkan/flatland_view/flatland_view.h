// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_VULKAN_FLATLAND_VIEW_FLATLAND_VIEW_H_
#define SRC_LIB_VULKAN_FLATLAND_VIEW_FLATLAND_VIEW_H_

#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

// This class provides a convenient wrapper around the Flatland. Only SDK available constructs
// should be used so that this code may be leveraged by Vulkan applications built with the SDK.
class FlatlandView {
 public:
  using ResizeCallback = fit::function<void(uint32_t width, uint32_t height)>;

  static std::unique_ptr<FlatlandView> Create(
      sys::ComponentContext* context, fuchsia::ui::views::ViewCreationToken view_creation_token,
      ResizeCallback resize_callback);

  explicit FlatlandView(ResizeCallback resize_callback);

  zx::channel TakeChildViewCreationToken() {
    FX_DCHECK(child_view_creation_token_.is_valid());
    return std::move(child_view_creation_token_);
  }

  bool Init(sys::ComponentContext* context,
            fuchsia::ui::views::ViewCreationToken view_creation_token);

 private:
  void OnGetLayout(fuchsia::ui::composition::LayoutInfo info);
  void OnError(fuchsia::ui::composition::FlatlandError error);
  void Present();
  void OnNextFrameBegin(fuchsia::ui::composition::OnNextFrameBeginValues values);

  fuchsia::ui::composition::FlatlandPtr flatland_;
  fuchsia::ui::composition::ParentViewportWatcherPtr parent_viewport_watcher_;

  zx::channel viewport_creation_token_;
  zx::channel child_view_creation_token_;
  ResizeCallback resize_callback_;

  uint32_t present_credits_ = 1;
  bool pending_present_ = false;

  friend class FlatlandViewTest;
};

class FlatlandViewProviderService : public fuchsia::ui::app::ViewProvider {
 public:
  using CreateView2Callback = fit::function<void(fuchsia::ui::app::CreateView2Args args)>;

  FlatlandViewProviderService(sys::ComponentContext* context,
                              CreateView2Callback create_view_callback);

  // fuchsia::ui::app::ViewProvider methods.
  void CreateView(zx::eventpair view_token,
                  fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
                  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services) override;
  void CreateViewWithViewRef(zx::eventpair view_token,
                             fuchsia::ui::views::ViewRefControl view_ref_control,
                             fuchsia::ui::views::ViewRef view_ref) override;
  void CreateView2(fuchsia::ui::app::CreateView2Args args) override;

 private:
  void HandleViewProviderRequest(fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request);

  CreateView2Callback create_view_callback_;
  fidl::BindingSet<ViewProvider> bindings_;
};

#endif  // SRC_LIB_VULKAN_FLATLAND_VIEW_FLATLAND_VIEW_H_
