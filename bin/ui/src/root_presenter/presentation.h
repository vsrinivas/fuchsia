// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_ROOT_PRESENTER_PRESENTATION_H_
#define APPS_MOZART_SRC_ROOT_PRESENTER_PRESENTATION_H_

#include "apps/mozart/services/buffers/cpp/buffer_producer.h"
#include "apps/mozart/services/composition/compositor.fidl.h"
#include "apps/mozart/services/composition/cpp/frame_tracker.h"
#include "apps/mozart/services/geometry/geometry.fidl.h"
#include "apps/mozart/services/input/input_dispatcher.fidl.h"
#include "apps/mozart/services/views/view_manager.fidl.h"
#include "apps/mozart/services/views/views.fidl.h"
#include "apps/mozart/src/input_reader/input_interpreter.h"
#include "apps/mozart/src/input_reader/input_reader.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"
#include "third_party/skia/include/core/SkImage.h"

namespace root_presenter {

class Presentation : public mozart::input::InterpreterListener,
                     public mozart::ViewTreeListener,
                     public mozart::ViewListener,
                     public mozart::ViewContainerListener {
 public:
  Presentation(mozart::Compositor* compositor,
               mozart::ViewManager* view_manager,
               mozart::ViewOwnerPtr view_owner);

  ~Presentation() override;

  void Present(ftl::Closure shutdown_callback);

 private:
  // |InputInterpreterListener|:
  void OnEvent(mozart::InputEventPtr event) override;
  void OnDeviceAdded(const mozart::input::InputDevice* device) override;
  void OnDeviceRemoved(const mozart::input::InputDevice* device) override;

  // |ViewTreeListener|:
  void OnRendererDied(const OnRendererDiedCallback& callback) override;

  // |ViewContainerListener|:
  void OnChildAttached(uint32_t child_key,
                       mozart::ViewInfoPtr child_view_info,
                       const OnChildAttachedCallback& callback) override;
  void OnChildUnavailable(uint32_t child_key,
                          const OnChildUnavailableCallback& callback) override;

  // |ViewListener|:
  void OnInvalidation(mozart::ViewInvalidationPtr invalidation,
                      const OnInvalidationCallback& callback) override;

  void StartInput();
  void CreateViewTree();
  void LoadCursor();
  void UpdateRootViewProperties();
  void OnLayout();
  mozart::SceneMetadataPtr CreateSceneMetadata() const;
  void UpdateScene();

  void Shutdown();

  mozart::Compositor* const compositor_;
  mozart::ViewManager* const view_manager_;
  mozart::ViewOwnerPtr view_owner_;

  mozart::ViewPtr root_view_;
  mozart::ViewOwnerPtr root_view_owner_;
  mozart::ScenePtr root_scene_;

  ftl::Closure shutdown_callback_;

  mozart::RendererPtr renderer_;
  mozart::DisplayInfoPtr display_info_;

  mozart::input::InputInterpreter input_interpreter_;
  mozart::input::InputReader input_reader_;
  mozart::PointF mouse_coordinates_;

  fidl::Binding<mozart::ViewTreeListener> tree_listener_binding_;
  fidl::Binding<mozart::ViewContainerListener> tree_container_listener_binding_;
  fidl::Binding<mozart::ViewContainerListener> view_container_listener_binding_;
  fidl::Binding<mozart::ViewListener> view_listener_binding_;

  mozart::ViewTreePtr tree_;
  mozart::ViewContainerPtr tree_container_;
  mozart::ViewContainerPtr root_container_;
  mozart::InputDispatcherPtr input_dispatcher_;

  mozart::ViewInfoPtr content_view_info_;

  mozart::FrameTracker frame_tracker_;
  uint32_t scene_version_ = mozart::kSceneVersionNone;

  bool cursor_resources_uploaded_ = false;
  bool scene_resources_uploaded_ = false;
  bool layout_changed_ = true;
  bool show_cursor_ = false;
  mozart::PointF cursor_position_;
  sk_sp<SkImage> cursor_image_;
  mozart::BufferProducer buffer_producer_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Presentation);
};

}  // namespace root_presenter

#endif  // APPS_MOZART_SRC_ROOT_PRESENTER_PRESENTATION_H_
