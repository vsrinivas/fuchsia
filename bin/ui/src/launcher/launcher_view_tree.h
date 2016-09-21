// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_LAUNCHER_VIEW_TREE_IMPL_H_
#define SERVICES_UI_LAUNCHER_VIEW_TREE_IMPL_H_

#include "base/callback.h"
#include "base/macros.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "apps/mozart/services/composition/interfaces/compositor.mojom.h"
#include "mojo/services/native_viewport/interfaces/native_viewport.mojom.h"
#include "mojo/services/ui/input/interfaces/input_dispatcher.mojom.h"
#include "mojo/services/ui/views/interfaces/view_manager.mojom.h"

namespace launcher {

class LauncherViewTree : public mojo::ui::ViewTreeListener,
                         public mojo::ui::ViewContainerListener {
 public:
  LauncherViewTree(mojo::gfx::composition::Compositor* compositor,
                   mojo::ui::ViewManager* view_manager,
                   mojo::ContextProviderPtr context_provider,
                   mojo::ViewportMetricsPtr viewport_metrics,
                   const base::Closure& shutdown_callback);

  ~LauncherViewTree() override;

  void SetRoot(mojo::ui::ViewOwnerPtr owner);
  void SetViewportMetrics(mojo::ViewportMetricsPtr viewport_metrics);
  void DispatchEvent(mojo::EventPtr event);

 private:
  // |ViewTreeListener|:
  void OnRendererDied(const OnRendererDiedCallback& callback) override;

  // |ViewContainerListener|:
  void OnChildAttached(uint32_t child_key,
                       mojo::ui::ViewInfoPtr child_view_info,
                       const OnChildAttachedCallback& callback) override;
  void OnChildUnavailable(uint32_t child_key,
                          const OnChildUnavailableCallback& callback) override;

  void OnViewTreeConnectionError();
  void OnInputDispatcherConnectionError();

  void UpdateViewProperties();

  void Shutdown();

  mojo::gfx::composition::Compositor* compositor_;
  mojo::ui::ViewManager* view_manager_;

  mojo::ContextProviderPtr context_provider_;
  mojo::ViewportMetricsPtr viewport_metrics_;
  base::Closure shutdown_callback_;

  mojo::Binding<mojo::ui::ViewTreeListener> view_tree_listener_binding_;
  mojo::Binding<mojo::ui::ViewContainerListener>
      view_container_listener_binding_;

  mojo::ui::ViewTreePtr view_tree_;
  mojo::ui::ViewContainerPtr view_container_;
  mojo::ui::InputDispatcherPtr input_dispatcher_;

  uint32_t root_key_ = 0u;
  bool root_was_set_ = false;
  mojo::ui::ViewInfoPtr root_view_info_;

  DISALLOW_COPY_AND_ASSIGN(LauncherViewTree);
};

}  // namespace launcher

#endif  // SERVICES_UI_LAUNCHER_VIEW_TREE_IMPL_H_
