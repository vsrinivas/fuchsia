// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_UI_BASE_VIEW_H_
#define MOJO_UI_BASE_VIEW_H_

#include <string>

#include "apps/mozart/services/composition/cpp/frame_tracker.h"
#include "apps/mozart/services/composition/interfaces/scenes.mojom.h"
#include "apps/mozart/services/views/interfaces/view_manager.mojom.h"
#include "apps/mozart/services/views/interfaces/views.mojom.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/interface_handle.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/public/interfaces/application/application_connector.mojom.h"
#include "mojo/public/interfaces/application/service_provider.mojom.h"

namespace mozart {

// Abstract base implementation of a view for simple applications.
// Subclasses must handle layout and provide content for the scene by
// implementing the methods of the |ViewListener| mojom interface.
//
// It is not necessary to use this class to implement all Views.
// This class is merely intended to make the simple apps easier to write.
class BaseView : public ViewListener, public ViewContainerListener {
 public:
  BaseView(mojo::InterfaceHandle<mojo::ApplicationConnector> app_connector,
           mojo::InterfaceRequest<ViewOwner> view_owner_request,
           const std::string& label);

  ~BaseView() override;

  // Gets the application implementation object provided at creation time.
  mojo::ApplicationConnector* app_connector() { return app_connector_.get(); }

  // Gets the view manager.
  ViewManager* view_manager() { return view_manager_.get(); }

  // Gets the underlying view interface.
  View* view() { return view_.get(); }

  // Gets the service provider for the view.
  mojo::ServiceProvider* GetViewServiceProvider();

  // Gets the underlying view container interface.
  ViewContainer* GetViewContainer();

  // Gets the scene for the view.
  // Returns nullptr if the |TakeScene| was called.
  Scene* scene() { return scene_.get(); }

  // Takes the scene from the view.
  // This is useful if the scene will be rendered by a separate component.
  ScenePtr TakeScene() { return scene_.Pass(); }

  // Gets the currently requested scene version.
  // This information is updated before processing each invalidation.
  uint32_t scene_version() const { return scene_version_; }

  // Gets the current view properties.
  // This information is updated before processing each invalidation.
  // Returns nullptr if none.
  const ViewProperties* properties() const { return properties_.get(); }

  // Gets the frame tracker which maintains timing information for this view.
  // This information is updated before processing each invalidation.
  const FrameTracker& frame_tracker() const { return frame_tracker_; }

  // Creates scene metadata initialized using the scene version and
  // current frame's presentation time.
  SceneMetadataPtr CreateSceneMetadata() const;

  // Invalidates the view.
  //
  // See |View| interface for a full description.
  void Invalidate();

  // Called during invalidation when the view's properties have changed.
  //
  // The subclass should compare the old and new properties and make note of
  // whether these property changes will affect the layout or content of
  // the view.
  //
  // The default implementation does nothing.
  //
  // This method is only called when new view properties were supplied by
  // the view's parent.
  virtual void OnPropertiesChanged(ViewPropertiesPtr old_properties);

  // Called during invalidation to update its layout.
  //
  // The subclass should apply any necessary changes to its layout and to
  // the properties of its children.  Once this method returns, the view
  // container will be flushed to allow the children to proceed with their
  // own invalidations using these new properties.
  //
  // The default implementation does nothing.
  //
  // This method is called after |OnPropertiesChanged()|.
  // This method is not called if the view has not received properties yet.
  virtual void OnLayout();

  // Called during invalidation to draw the contents of the view.
  //
  // The subclass should update the contents of its scene and publish it
  // together with scene metadata generated using |CreateSceneMetadata()|.
  //
  // The default implementation does nothing.
  //
  // This method is called after |OnLayout()|.
  // This method is not called if the view has not received properties yet.
  virtual void OnDraw();

  // Called when a child is attached.
  virtual void OnChildAttached(uint32_t child_key, ViewInfoPtr child_view_info);

  // Called when a child becomes unavailable.
  virtual void OnChildUnavailable(uint32_t child_key);

 private:
  // |ViewListener|:
  void OnInvalidation(ViewInvalidationPtr invalidation,
                      const OnInvalidationCallback& callback) override;

  // |ViewContainerListener|:
  void OnChildAttached(uint32_t child_key,
                       ViewInfoPtr child_view_info,
                       const OnChildAttachedCallback& callback) override;
  void OnChildUnavailable(uint32_t child_key,
                          const OnChildUnavailableCallback& callback) override;

  mojo::ApplicationConnectorPtr app_connector_;

  mojo::StrongBinding<ViewListener> view_listener_binding_;
  mojo::Binding<ViewContainerListener> view_container_listener_binding_;
  ViewManagerPtr view_manager_;
  ViewPtr view_;
  mojo::ServiceProviderPtr view_service_provider_;
  ViewContainerPtr view_container_;
  ScenePtr scene_;
  FrameTracker frame_tracker_;
  uint32_t scene_version_ = kSceneVersionNone;
  ViewPropertiesPtr properties_;
  bool invalidated_ = false;

  FTL_DISALLOW_COPY_AND_ASSIGN(BaseView);
};

}  // namespace mozart

#endif  // MOJO_UI_BASE_VIEW_H_
