// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_LAUNCHER_LAUNCHER_VIEW_TREE_H_
#define APPS_MOZART_SRC_LAUNCHER_LAUNCHER_VIEW_TREE_H_

#include "apps/mozart/services/composition/compositor.fidl.h"
#include "apps/mozart/services/input/input_dispatcher.fidl.h"
#include "apps/mozart/services/views/view_manager.fidl.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace launcher {

class LauncherViewTree : public mozart::ViewTreeListener,
                         public mozart::ViewContainerListener {
 public:
  LauncherViewTree(mozart::ViewManager* view_manager,
                   mozart::RendererPtr renderer,
                   mozart::DisplayInfoPtr display_info,
                   mozart::ViewOwnerPtr root_view,
                   const ftl::Closure& shutdown_callback);

  ~LauncherViewTree() override;

  void DispatchEvent(mozart::EventPtr event);

 private:
  // |ViewTreeListener|:
  void OnRendererDied(const OnRendererDiedCallback& callback) override;

  // |ViewContainerListener|:
  void OnChildAttached(uint32_t child_key,
                       mozart::ViewInfoPtr child_view_info,
                       const OnChildAttachedCallback& callback) override;
  void OnChildUnavailable(uint32_t child_key,
                          const OnChildUnavailableCallback& callback) override;

  void OnViewTreeConnectionError();
  void OnInputDispatcherConnectionError();

  void UpdateViewProperties();

  void Shutdown();

  mozart::ViewManager* view_manager_;

  mozart::DisplayInfoPtr display_info_;

  ftl::Closure shutdown_callback_;

  fidl::Binding<mozart::ViewTreeListener> view_tree_listener_binding_;
  fidl::Binding<mozart::ViewContainerListener> view_container_listener_binding_;

  mozart::ViewTreePtr view_tree_;
  mozart::ViewContainerPtr view_container_;
  mozart::InputDispatcherPtr input_dispatcher_;

  uint32_t root_key_ = 0u;
  bool root_was_set_ = false;
  mozart::ViewInfoPtr root_view_info_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LauncherViewTree);
};

}  // namespace launcher

#endif  // APPS_MOZART_SRC_LAUNCHER_LAUNCHER_VIEW_TREE_H_
