// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_VIEW_FRAMEWORK_BASE_VIEW_H_
#define LIB_UI_VIEW_FRAMEWORK_BASE_VIEW_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include <lib/fit/function.h>

#include <memory>
#include <string>

#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/interface_handle.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/scenic/client/session.h"

namespace mozart {

// Abstract base implementation of a view for simple applications.
// Subclasses must handle layout and provide content for the scene by
// implementing the methods of the |ViewListener| fidl interface.
//
// It is not necessary to use this class to implement all Views.
// This class is merely intended to make the simple apps easier to write.
class BaseView : private ::fuchsia::ui::views_v1::ViewListener,
                 private ::fuchsia::ui::views_v1::ViewContainerListener,
                 private fuchsia::ui::input::InputListener {
 public:
  BaseView(::fuchsia::ui::views_v1::ViewManagerPtr view_manager,
           fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner>
               view_owner_request,
           const std::string& label);

  ~BaseView() override;

  // Gets the view manager.
  ::fuchsia::ui::views_v1::ViewManager* view_manager() {
    return view_manager_.get();
  }

  // Gets the underlying view interface.
  ::fuchsia::ui::views_v1::View* view() { return view_.get(); }

  // Gets the service provider for the view.
  fuchsia::sys::ServiceProvider* GetViewServiceProvider();

  // Gets the underlying view container interface.
  ::fuchsia::ui::views_v1::ViewContainer* GetViewContainer();

  // Gets a wrapper for the view's session.
  scenic::Session* session() { return &session_; }

  // Gets the imported parent node to which the session's tree of nodes
  // should be attached.
  scenic::ImportNode& parent_node() { return parent_node_; }

  // Gets the current view properties.
  // Returns nullptr if unknown.
  const ::fuchsia::ui::views_v1::ViewProperties& properties() const {
    return properties_;
  }

  // Returns true if the view has a non-empty size in logical pixels.
  bool has_logical_size() const {
    return logical_size_.width > 0.f && logical_size_.height > 0.f;
  }

  // Gets the size of the view in logical pixels.
  // This value is zero until the view receives a layout from its parent.
  const fuchsia::math::SizeF& logical_size() const { return logical_size_; }

  // Returns true if the view has a non-empty size in physical pixels.
  bool has_physical_size() const {
    return physical_size_.width > 0 && physical_size_.height > 0;
  }

  // Gets the size of the view in physical pixels.
  // This value is zero until the view receives a layout from its parent
  // and metrics from its session.
  const fuchsia::math::Size& physical_size() const { return physical_size_; }

  // When true, the session provided metrics are adjusted such that the
  // X and Y scale factors are made equal before computing the physical size.
  // The default is false in which case the X and Y scale factors may differ.
  bool need_square_metrics() const { return need_square_metrics_; }
  void SetNeedSquareMetrics(bool enable);

  // Returns true if the view has received metrics from its session.
  bool has_metrics() const {
    return adjusted_metrics_.scale_x > 0.f && adjusted_metrics_.scale_y > 0.f &&
           adjusted_metrics_.scale_z > 0.f;
  }

  // Gets the view's metrics.
  // This value is zero until the view receives metrics from its session.
  const fuchsia::ui::gfx::Metrics& metrics() const { return adjusted_metrics_; }

  // Gets the input connection.
  fuchsia::ui::input::InputConnection* input_connection() {
    return input_connection_.get();
  }

  // Sets a callback which is invoked when the view's owner releases the
  // view causing the view manager to unregister it.
  //
  // This should be used to implement cleanup policies to release resources
  // associated with the view (including the object itself).
  void SetReleaseHandler(fit::closure callback);

  // Invalidates the scene, causing |OnSceneInvalidated()| to be invoked
  // during the next frame.
  void InvalidateScene();

  // Called when the view's properties have changed.
  //
  // The subclass should compare the old and new properties and make note of
  // whether these property changes will affect the layout or content of
  // the view then update accordingly.
  //
  // The default implementation does nothing.
  virtual void OnPropertiesChanged(
      ::fuchsia::ui::views_v1::ViewProperties old_properties);

  // Called when it's time for the view to update its scene contents due to
  // invalidation.  The new contents are presented once this function returns.
  //
  // The default implementation does nothing.
  virtual void OnSceneInvalidated(
      fuchsia::images::PresentationInfo presentation_info);

  // Called when session events are received.
  //
  // The default implementation does nothing.
  virtual void OnSessionEvent(
      fidl::VectorPtr<fuchsia::ui::scenic::Event> events);

  // Called to handle an input event.
  // Returns true if the view will handle the event, false if the event
  // should continue propagating to other views which may handle it themselves.
  //
  // The default implementation returns false.
  virtual bool OnInputEvent(fuchsia::ui::input::InputEvent event);

  // Called when a child is attached.
  //
  // The default implementation does nothing.
  virtual void OnChildAttached(
      uint32_t child_key, ::fuchsia::ui::views_v1::ViewInfo child_view_info);

  // Called when a child becomes unavailable.
  //
  // The default implementation does nothing.
  virtual void OnChildUnavailable(uint32_t child_key);

 private:
  // |ViewListener|:
  void OnPropertiesChanged(::fuchsia::ui::views_v1::ViewProperties properties,
                           OnPropertiesChangedCallback callback) override;

  // |ViewContainerListener|:
  void OnChildAttached(uint32_t child_key,
                       ::fuchsia::ui::views_v1::ViewInfo child_view_info,
                       OnChildAttachedCallback callback) override;
  void OnChildUnavailable(uint32_t child_key,
                          OnChildUnavailableCallback callback) override;

  // |InputListener|:
  void OnEvent(fuchsia::ui::input::InputEvent event,
               OnEventCallback callback) override;

  void PresentScene(zx_time_t presentation_time);
  void HandleSessionEvents(fidl::VectorPtr<fuchsia::ui::scenic::Event> events);
  void AdjustMetricsAndPhysicalSize();

  ::fuchsia::ui::views_v1::ViewManagerPtr view_manager_;
  fidl::Binding<::fuchsia::ui::views_v1::ViewListener> view_listener_binding_;
  fidl::Binding<::fuchsia::ui::views_v1::ViewContainerListener>
      view_container_listener_binding_;
  fidl::Binding<fuchsia::ui::input::InputListener> input_listener_binding_;

  ::fuchsia::ui::views_v1::ViewPtr view_;
  fuchsia::sys::ServiceProviderPtr view_service_provider_;
  ::fuchsia::ui::views_v1::ViewContainerPtr view_container_;
  fuchsia::ui::input::InputConnectionPtr input_connection_;
  ::fuchsia::ui::views_v1::ViewProperties properties_;
  fuchsia::math::SizeF logical_size_;
  fuchsia::math::Size physical_size_;
  bool need_square_metrics_ = false;
  fuchsia::ui::gfx::Metrics original_metrics_;
  fuchsia::ui::gfx::Metrics adjusted_metrics_;
  scenic::Session session_;
  scenic::ImportNode parent_node_;

  bool invalidate_pending_ = false;
  bool present_pending_ = false;

  zx_time_t last_presentation_time_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(BaseView);
};

}  // namespace mozart

#endif  // LIB_UI_VIEW_FRAMEWORK_BASE_VIEW_H_
