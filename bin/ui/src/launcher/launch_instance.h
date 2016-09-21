// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_LAUNCHER_LAUNCH_INSTANCE_H_
#define SERVICES_UI_LAUNCHER_LAUNCH_INSTANCE_H_

#include <memory>

#include "base/callback.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/services/gfx/composition/interfaces/compositor.mojom.h"
#include "mojo/services/native_viewport/interfaces/native_viewport.mojom.h"
#include "mojo/services/native_viewport/interfaces/native_viewport_event_dispatcher.mojom.h"
#include "mojo/services/ui/views/interfaces/view_manager.mojom.h"
#include "mojo/services/ui/views/interfaces/view_provider.mojom.h"

namespace launcher {

class LauncherViewTree;

class LaunchInstance : public mojo::NativeViewportEventDispatcher {
 public:
  LaunchInstance(mojo::NativeViewportPtr viewport,
                 mojo::ui::ViewProviderPtr view_provider,
                 mojo::gfx::composition::Compositor* compositor,
                 mojo::ui::ViewManager* view_manager,
                 const base::Closure& shutdown_callback);
  ~LaunchInstance() override;

  void Launch();

 private:
  // |NativeViewportEventDispatcher|:
  void OnEvent(mojo::EventPtr event,
               const mojo::Callback<void()>& callback) override;

  void InitViewport();
  void OnViewportConnectionError();
  void OnViewportCreated(mojo::ViewportMetricsPtr metrics);
  void OnViewportMetricsChanged(mojo::ViewportMetricsPtr metrics);
  void RequestUpdatedViewportMetrics();

  mojo::NativeViewportPtr viewport_;
  mojo::ui::ViewProviderPtr view_provider_;

  mojo::gfx::composition::Compositor* const compositor_;
  mojo::ui::ViewManager* const view_manager_;
  base::Closure shutdown_callback_;

  mojo::Binding<NativeViewportEventDispatcher>
      viewport_event_dispatcher_binding_;

  std::unique_ptr<LauncherViewTree> view_tree_;

  mojo::ui::ViewOwnerPtr client_view_owner_;

  DISALLOW_COPY_AND_ASSIGN(LaunchInstance);
};

}  // namespace launcher

#endif  // SERVICES_UI_LAUNCHER_LAUNCH_INSTANCE_H_
