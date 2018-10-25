// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_VMM_GUEST_VIEW_H_
#define GARNET_BIN_GUEST_VMM_GUEST_VIEW_H_

#include <fuchsia/guest/device/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/ui/view_framework/base_view.h>

#include "garnet/lib/machina/gpu_scanout.h"

class GuestView : public mozart::BaseView {
 public:
  GuestView(
      machina::GpuScanout* scanout,
      fidl::InterfaceHandle<fuchsia::ui::input::InputListener> input_listener,
      fuchsia::guest::device::ViewListenerPtr view_listener,
      fuchsia::ui::viewsv1::ViewManagerPtr view_manager,
      fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
          view_owner_request);

 private:
  scenic::ShapeNode background_node_;
  scenic::Material material_;
  fuchsia::images::ImageInfo image_info_;
  std::unique_ptr<scenic::Memory> memory_;
  uint32_t scanout_source_width_;
  uint32_t scanout_source_height_;

  machina::GpuScanout* scanout_;
  fuchsia::guest::device::ViewListenerPtr view_listener_;

  // |mozart::BaseView|
  void OnPropertiesChanged(
      fuchsia::ui::viewsv1::ViewProperties old_properties) override;
  void OnSceneInvalidated(
      fuchsia::images::PresentationInfo presentation_info) override;
};

class ScenicScanout : public fuchsia::ui::viewsv1::ViewProvider {
 public:
  ScenicScanout(
      component::StartupContext* startup_context,
      fidl::InterfaceHandle<fuchsia::ui::input::InputListener> input_listener,
      fuchsia::guest::device::ViewListenerPtr view_listener,
      machina::GpuScanout* scanout);

 private:
  machina::GpuScanout* scanout_;
  fidl::InterfaceHandle<fuchsia::ui::input::InputListener> input_listener_;
  fuchsia::guest::device::ViewListenerPtr view_listener_;
  component::StartupContext* startup_context_;
  fidl::BindingSet<ViewProvider> bindings_;
  std::unique_ptr<GuestView> view_;

  // |fuchsia::ui::viewsv1::ViewProvider|
  void CreateView(fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
                      view_owner_request,
                  fidl::InterfaceRequest<fuchsia::sys::ServiceProvider>
                      view_services) override;
};

#endif  // GARNET_BIN_GUEST_VMM_GUEST_VIEW_H_
