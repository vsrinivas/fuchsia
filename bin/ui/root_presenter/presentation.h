// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_ROOT_PRESENTER_PRESENTATION_H_
#define GARNET_BIN_UI_ROOT_PRESENTER_PRESENTATION_H_

#include <map>
#include <memory>

#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/ui/geometry/fidl/geometry.fidl.h"
#include "lib/ui/input/device_state.h"
#include "lib/ui/input/fidl/input_dispatcher.fidl.h"
#include "lib/ui/input/input_device_impl.h"
#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/views/fidl/view_manager.fidl.h"
#include "lib/ui/views/fidl/views.fidl.h"

namespace root_presenter {

// This class creates a view tree and sets up rendering of a new scene to
// display the graphical content of the view passed to |PresentScene()|.  It
// also wires up input dispatch and manages the mouse cursor.
//
// The view tree consists of a root view which is implemented by this class
// and which has the presented (content) view as its child.
//
// The scene's node tree has the following structure:
// + Scene
//   + RootViewHost
//     + link: root_view_host_import_token
//       + RootView's view manager stub
//         + link: root_view_parent_export_token
//           + RootView
//             + link: content_view_host_import_token
//               + child: ContentViewHost
//           + link: Content view's actual content
//   + child: cursor 1
//   + child: cursor N
class Presentation : private mozart::ViewTreeListener,
                     private mozart::ViewListener,
                     private mozart::ViewContainerListener {
 public:
  Presentation(mozart::ViewManager* view_manager,
               scenic::SceneManager* scene_manager);

  ~Presentation() override;

  // Present the specified view.
  // Invokes the callback if an error occurs.
  // This method must be called at most once for the lifetime of the
  // presentation.
  void Present(mozart::ViewOwnerPtr view_owner, fxl::Closure shutdown_callback);

  void OnReport(uint32_t device_id, mozart::InputReportPtr report);
  void OnDeviceAdded(mozart::InputDeviceImpl* input_device);
  void OnDeviceRemoved(uint32_t device_id);

 private:
  // |ViewContainerListener|:
  void OnChildAttached(uint32_t child_key,
                       mozart::ViewInfoPtr child_view_info,
                       const OnChildAttachedCallback& callback) override;
  void OnChildUnavailable(uint32_t child_key,
                          const OnChildUnavailableCallback& callback) override;

  // |ViewListener|:
  void OnPropertiesChanged(
      mozart::ViewPropertiesPtr properties,
      const OnPropertiesChangedCallback& callback) override;

  void CreateViewTree(mozart::ViewOwnerPtr view_owner,
                      scenic::DisplayInfoPtr display_info);
  void OnEvent(mozart::InputEventPtr event);

  void PresentScene();
  void Shutdown();

  // Handle the "Perspective Demo" hotkey.  This cycles through the following
  // modes:
  // 1) default UI behavior
  // 2) disable clipping
  // 3) disable clipping + zoomed out perspective view w/ trackball
  // ... and then back to 1).
  //
  // In mode 3), dragging along the bottom 10% of the screen causes the camera
  // to pan/rotate around the stage.
  void HandleAltBackspace();
  bool UpdateAnimation(uint64_t presentation_time);

  mozart::ViewManager* const view_manager_;
  scenic::SceneManager* const scene_manager_;

  scenic_lib::Session session_;
  scenic_lib::DisplayCompositor compositor_;
  scenic_lib::LayerStack layer_stack_;
  scenic_lib::Layer layer_;
  scenic_lib::Renderer renderer_;
  // TODO(MZ-254): put camera before scene.
  scenic_lib::Scene scene_;
  scenic_lib::Camera camera_;
  scenic_lib::EntityNode root_view_host_node_;
  zx::eventpair root_view_host_import_token_;
  scenic_lib::ImportNode root_view_parent_node_;
  zx::eventpair root_view_parent_export_token_;
  scenic_lib::EntityNode content_view_host_node_;
  zx::eventpair content_view_host_import_token_;
  scenic_lib::RoundedRectangle cursor_shape_;
  scenic_lib::Material cursor_material_;

  scenic::DisplayInfoPtr display_info_;
  float logical_width_ = 0.f;
  float logical_height_ = 0.f;
  float device_pixel_ratio_ = 1.f;

  mozart::ViewPtr root_view_;

  fxl::Closure shutdown_callback_;

  mozart::PointF mouse_coordinates_;

  fidl::Binding<mozart::ViewTreeListener> tree_listener_binding_;
  fidl::Binding<mozart::ViewContainerListener> tree_container_listener_binding_;
  fidl::Binding<mozart::ViewContainerListener> view_container_listener_binding_;
  fidl::Binding<mozart::ViewListener> view_listener_binding_;

  mozart::ViewTreePtr tree_;
  mozart::ViewContainerPtr tree_container_;
  mozart::ViewContainerPtr root_container_;
  mozart::InputDispatcherPtr input_dispatcher_;

  enum AnimationState {
    kDefault,
    kNoClipping,
    kCameraMovingAway,
    kCameraReturning,
    kTrackball,
  };
  AnimationState animation_state_ = kDefault;

  // Presentation time at which this presentation last entered either
  // kCameraMovingAway or kCameraReturning state.
  uint64_t animation_start_time_ = 0;

  // State related to managing camera panning in "trackball" mode.
  bool trackball_pointer_down_ = false;
  uint32_t trackball_device_id_ = 0;
  uint32_t trackball_pointer_id_ = 0;
  float trackball_previous_x_ = 0.f;
  float camera_pan_ = 0.f;

  struct CursorState {
    bool created;
    bool visible;
    mozart::PointF position;
    std::unique_ptr<scenic_lib::ShapeNode> node;
  };

  std::map<uint32_t, CursorState> cursors_;
  std::map<
      uint32_t,
      std::pair<mozart::InputDeviceImpl*, std::unique_ptr<mozart::DeviceState>>>
      device_states_by_id_;

  fxl::WeakPtrFactory<Presentation> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Presentation);
};

}  // namespace root_presenter

#endif  // GARNET_BIN_UI_ROOT_PRESENTER_PRESENTATION_H_
