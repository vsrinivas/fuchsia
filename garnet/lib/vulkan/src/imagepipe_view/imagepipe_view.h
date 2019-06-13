// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_SRC_IMAGEPIPE_VIEW_IMAGEPIPE_VIEW_H
#define GARNET_LIB_VULKAN_SRC_IMAGEPIPE_VIEW_IMAGEPIPE_VIEW_H

#include <fuchsia/images/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <memory>

// This class provides a convenient wrapper around the scenic fidl apis
// needed to get an imagepipe. Only SDK available constructs should be used so
// that this code may be leveraged by Vulkan applications built with the SDK.
class ImagePipeView : public fuchsia::ui::scenic::SessionListener {
 public:
  using ResizeCallback = fit::function<void(float width, float height)>;

  static std::unique_ptr<ImagePipeView> Create(
      sys::ComponentContext* context, fuchsia::ui::views::ViewToken view_token,
      ResizeCallback resize_callback);

  ImagePipeView(ResizeCallback resize_callback);

  zx::channel TakeImagePipeChannel() { return std::move(image_pipe_endpoint_); }

  bool Init(sys::ComponentContext* context,
            fuchsia::ui::views::ViewToken view_token);

  // fuchsia::ui::scenic::SessionListener methods.
  void OnScenicEvent(std::vector<fuchsia::ui::scenic::Event> events) override;
  void OnScenicError(std::string error) override;

 private:
  void OnViewPropertiesChanged(fuchsia::ui::gfx::ViewProperties vp);

  fidl::Binding<fuchsia::ui::scenic::SessionListener> session_listener_binding_;
  fuchsia::ui::scenic::SessionPtr session_;
  zx::channel image_pipe_endpoint_;
  ResizeCallback resize_callback_;

  int new_resource_id_;

  float view_width_ = 0;
  float view_height_ = 0;

  friend class ImagePipeViewTest;
};

class ImagePipeViewProviderService : public fuchsia::ui::app::ViewProvider {
 public:
  using CreateViewCallback =
      fit::function<void(fuchsia::ui::views::ViewToken view_token)>;

  ImagePipeViewProviderService(sys::ComponentContext* context,
                               CreateViewCallback create_view_callback);

  // fuchsia::ui::app::ViewProvider methods.
  void CreateView(
      zx::eventpair view_token,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
      fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services)
      override;

 private:
  void HandleViewProviderRequest(
      fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request);

  CreateViewCallback create_view_callback_;
  fidl::BindingSet<ViewProvider> bindings_;
};

#endif  // GARNET_LIB_VULKAN_SRC_IMAGEPIPE_VIEW_IMAGEPIPE_VIEW_H
