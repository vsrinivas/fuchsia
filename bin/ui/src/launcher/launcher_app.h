// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_LAUNCHER_LAUNCHER_APP_H_
#define SERVICES_UI_LAUNCHER_LAUNCHER_APP_H_

#include <memory>
#include <unordered_map>
#include <vector>

#include "mojo/common/tracing_impl.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "services/ui/launcher/launch_instance.h"
#include "services/ui/launcher/launcher.mojom.h"

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
  void LaunchOnViewport(
      mojo::InterfaceHandle<mojo::NativeViewport> viewport,
      mojo::InterfaceHandle<mojo::ui::ViewProvider> view_provider) override;

  void LaunchInternal(mojo::NativeViewportPtr viewport,
                      mojo::ui::ViewProviderPtr view_provider);
  void OnLaunchTermination(uint32_t id);

  void OnCompositorConnectionError();
  void OnViewManagerConnectionError();
  void OnViewAssociateConnectionError();

  mojo::TracingImpl tracing_;

  mojo::BindingSet<Launcher> bindings_;
  std::unordered_map<uint32_t, std::unique_ptr<LaunchInstance>>
      launch_instances_;

  uint32_t next_id_;

  mojo::gfx::composition::CompositorPtr compositor_;
  mojo::ui::ViewManagerPtr view_manager_;
  std::vector<mojo::ui::ViewAssociateOwnerPtr> view_associate_owners_;

  DISALLOW_COPY_AND_ASSIGN(LauncherApp);
};

}  // namespace launcher

#endif  // SERVICES_UI_LAUNCHER_LAUNCHER_APP_H_
