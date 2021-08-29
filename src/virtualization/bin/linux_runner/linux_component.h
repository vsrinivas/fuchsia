// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_LINUX_RUNNER_LINUX_COMPONENT_H_
#define SRC_VIRTUALIZATION_BIN_LINUX_RUNNER_LINUX_COMPONENT_H_

#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/outgoing_directory.h>
#include <lib/zx/eventpair.h>

namespace linux_runner {

// Represents a single linux mod with an associated ViewProvider.
class LinuxComponent : public fuchsia::sys::ComponentController,
                       public fuchsia::ui::app::ViewProvider {
 public:
  using TerminationCallback = fit::function<void(uint32_t)>;
  static std::unique_ptr<LinuxComponent> Create(
      TerminationCallback termination_callback, fuchsia::sys::Package package,
      fuchsia::sys::StartupInfo startup_info,
      fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller,
      fuchsia::ui::app::ViewProviderPtr remote_view_provider, uint32_t id);

  ~LinuxComponent();

 private:
  TerminationCallback termination_callback_;
  fidl::Binding<fuchsia::sys::ComponentController> application_controller_;
  fidl::InterfaceRequest<fuchsia::io::Directory> directory_request_;
  sys::OutgoingDirectory outgoing_;
  fidl::BindingSet<fuchsia::ui::app::ViewProvider> view_bindings_;
  fuchsia::ui::app::ViewProviderPtr remote_view_provider_;
  const uint32_t id_;

  LinuxComponent(TerminationCallback termination_callback, fuchsia::sys::Package package,
                 fuchsia::sys::StartupInfo startup_info,
                 fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller,
                 fuchsia::ui::app::ViewProviderPtr remote_view_provider, uint32_t id);

  // |fuchsia::sys::ComponentController|
  void Kill() override;
  void Detach() override;

  // |fuchsia::ui::app::ViewProvider|
  void CreateView(zx::eventpair view_token,
                  fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
                  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services) override;
  void CreateViewWithViewRef(zx::eventpair token,
                             fuchsia::ui::views::ViewRefControl view_ref_control,
                             fuchsia::ui::views::ViewRef view_ref) override;
  void CreateView2(fuchsia::ui::app::CreateView2Args args) override;
};

}  // namespace linux_runner

#endif  // SRC_VIRTUALIZATION_BIN_LINUX_RUNNER_LINUX_COMPONENT_H_
