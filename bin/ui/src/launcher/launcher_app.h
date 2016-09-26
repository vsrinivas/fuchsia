// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_LAUNCHER_LAUNCHER_APP_H_
#define SERVICES_UI_LAUNCHER_LAUNCHER_APP_H_

#include <memory>
#include <unordered_map>
#include <vector>

#include "apps/mozart/services/launcher/interfaces/launcher.mojom.h"
#include "apps/mozart/src/launcher/launch_instance.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/bindings/binding_set.h"

namespace launcher {

class LauncherApp : public mojo::ApplicationImplBase, public Launcher {
 public:
  LauncherApp();
  ~LauncherApp() override;

 private:
  // |ApplicationImplBase|:
  void OnInitialize() override;
  void InitCompositor();
  void InitViewManager();
  void InitViewAssociates(const std::string& associate_urls_command_line_param);
  bool OnAcceptConnection(
      mojo::ServiceProviderImpl* service_provider_impl) override;

  // |Launcher|:
  void Launch(const mojo::String& application_url) override;

  void LaunchInternal(mojo::InterfaceHandle<mojo::Framebuffer> framebuffer,
                      mojo::FramebufferInfoPtr framebuffer_info,
                      mojo::ui::ViewProviderPtr view_provider);
  void OnLaunchTermination(uint32_t id);

  void OnCompositorConnectionError();
  void OnViewManagerConnectionError();
  void OnViewAssociateConnectionError();

  mojo::BindingSet<Launcher> bindings_;
  std::unordered_map<uint32_t, std::unique_ptr<LaunchInstance>>
      launch_instances_;

  uint32_t next_id_;

  mojo::FramebufferProviderPtr framebuffer_provider_;

  mojo::gfx::composition::CompositorPtr compositor_;
  mojo::ui::ViewManagerPtr view_manager_;
  std::vector<mojo::ui::ViewAssociateOwnerPtr> view_associate_owners_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LauncherApp);
};

}  // namespace launcher

#endif  // SERVICES_UI_LAUNCHER_LAUNCHER_APP_H_
