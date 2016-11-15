// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_ROOT_PRESENTER_PRESENTATION_H_
#define APPS_MOZART_SRC_ROOT_PRESENTER_PRESENTATION_H_

#include "apps/mozart/services/composition/compositor.fidl.h"
#include "apps/mozart/services/input/input_dispatcher.fidl.h"
#include "apps/mozart/services/views/view_manager.fidl.h"
#include "apps/mozart/src/input_reader/input_interpreter.h"
#include "apps/mozart/src/input_reader/input_reader.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"

namespace root_presenter {

class Presentation : public mozart::ViewTreeListener,
                     public mozart::ViewContainerListener {
 public:
  Presentation(mozart::Compositor* compositor,
               mozart::ViewManager* view_manager,
               mozart::ViewOwnerPtr view_owner);

  ~Presentation() override;

  void Present(ftl::Closure shutdown_callback);

 private:
  // |ViewTreeListener|:
  void OnRendererDied(const OnRendererDiedCallback& callback) override;

  // |ViewContainerListener|:
  void OnChildAttached(uint32_t child_key,
                       mozart::ViewInfoPtr child_view_info,
                       const OnChildAttachedCallback& callback) override;
  void OnChildUnavailable(uint32_t child_key,
                          const OnChildUnavailableCallback& callback) override;

  void StartInput();
  void CreateViewTree();
  void UpdateViewProperties();

  void Shutdown();

  mozart::Compositor* const compositor_;
  mozart::ViewManager* const view_manager_;
  mozart::ViewOwnerPtr view_owner_;

  ftl::Closure shutdown_callback_;

  mozart::RendererPtr renderer_;
  mozart::DisplayInfoPtr display_info_;

  mozart::input::InputInterpreter input_interpreter_;
  mozart::input::InputReader input_reader_;
  mozart::PointF mouse_coordinates_;

  fidl::Binding<mozart::ViewTreeListener> view_tree_listener_binding_;
  fidl::Binding<mozart::ViewContainerListener> view_container_listener_binding_;

  mozart::ViewTreePtr view_tree_;
  mozart::ViewContainerPtr view_container_;
  mozart::InputDispatcherPtr input_dispatcher_;

  uint32_t root_key_ = 0u;
  mozart::ViewInfoPtr root_view_info_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Presentation);
};

}  // namespace root_presenter

#endif  // APPS_MOZART_SRC_ROOT_PRESENTER_PRESENTATION_H_
