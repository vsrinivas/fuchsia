// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_SCENIC_CPP_BASE_VIEW_H_
#define LIB_UI_SCENIC_CPP_BASE_VIEW_H_

#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>

#include "lib/app/cpp/startup_context.h"
#include "lib/svc/cpp/services.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/scenic/cpp/session.h"

namespace scenic {

// Abstract base implementation of a view for simple applications.
// Subclasses must handle layout and provide content for the scene by
// overriding the virtual methods defined in this class.
//
// It is not necessary to use this class to implement all Views.
// This class is merely intended to make the simple apps easier to write.
class BaseView : private fuchsia::ui::scenic::SessionListener {
 public:
  // Subclasses are typically created by ViewProviderService::CreateView(),
  // which provides the necessary args to pass down to this base class.
  BaseView(fuchsia::sys::StartupContext* startup_context,
           scenic::SessionPtrAndListenerRequest session_and_listener,
           zx::eventpair view_token, const std::string& debug_name);

  BaseView(const BaseView&) = delete;

  const View& view() const { return view_; }
  Session* session() { return &session_; }

  fuchsia::ui::gfx::ViewProperties view_properties() const {
    return view_properties_;
  }

  // Returns true if the view has a non-empty size in logical pixels.
  bool has_logical_size() const {
    auto& sz = logical_size();
    return sz.x > 0.f && sz.y > 0.f && sz.z > 0.f;
  }

  // Gets the size of the view in logical pixels.
  // This value is zero until the view receives a layout from its parent.
  const fuchsia::ui::gfx::vec3& logical_size() const { return logical_size_; }

  // Returns true if the view has a non-empty size in physical pixels.
  bool has_physical_size() const {
    auto& sz = physical_size();
    return sz.x > 0.f && sz.y > 0.f && sz.z > 0.f;
  }

  // Gets the size of the view in physical pixels.
  // This value is zero until the view receives a layout from its parent
  // and metrics from its session.
  const fuchsia::ui::gfx::vec3& physical_size() const {
    // TODO(SCN-809): use logical size for now.  Needs metrics to be provided
    // by Scenic.
    return logical_size();
  }

  // Sets a callback which is invoked when the view's owner releases the
  // view causing the view manager to unregister it.
  //
  // This should be used to implement cleanup policies to release resources
  // associated with the view (including the object itself).
  void SetReleaseHandler(fit::closure callback);

  // Serves as the return value for LaunchAppAndCreateView(), below.
  struct EmbeddedViewInfo {
    // Controls the launched app.  The app will be destroyed if this connection
    // is closed.
    fuchsia::sys::ComponentControllerPtr controller;

    // Services provided by the launched app.  Must not be destroyed
    // immediately, otherwise the |view_provider| connection may not be
    // established.
    fuchsia::sys::Services app_services;

    // ViewProvider service obtained from the app via |app_services|.  Must not
    // be destroyed immediately, otherwise the call to CreateView() might not
    // be processed.
    fuchsia::ui::app::ViewProviderPtr view_provider;

    // A token that can be used to create a ViewHolder; the corresponding token
    // was provided to |view_provider| via ViewProvider.CreateView().  The
    // launched app is expected to create a View, which will be connected to
    // the ViewHolder created with this token.
    zx::eventpair view_holder_token;

    // Handle to services provided by ViewProvider.CreateView().
    fidl::InterfaceHandle<fuchsia::sys::ServiceProvider>
        services_from_child_view;

    // Interface request for services provided to ViewProvider.CreateView();
    // the caller of LaunchAppAndCreateView() may choose to attach this request
    // to a ServiceProvider implementation.
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider>
        services_to_child_view;
  };

  // Launch an app and connect to its ViewProvider service, passing it the
  // necessary information to attach itself as a child view.  Populates the
  // returned EmbeddedViewInfo, which the caller can use to embed the child.
  // For example, an interface to a ViewProvider is obtained, a pair of
  // zx::eventpairs is created, CreateView is called, etc.  This encapsulates
  // the boilerplate the the client would otherwise write themselves.
  EmbeddedViewInfo LaunchAppAndCreateView(std::string app_url);

  // Invalidates the scene, causing |OnSceneInvalidated()| to be invoked
  // during the next frame.
  void InvalidateScene();

  // Called when it's time for the view to update its scene contents due to
  // invalidation.  The new contents are presented once this function returns.
  //
  // The default implementation does nothing.
  virtual void OnSceneInvalidated(
      fuchsia::images::PresentationInfo presentation_info);

  // Called when the view's properties have changed.
  //
  // The subclass should compare the old and new properties and make note of
  // whether these property changes will affect the layout or content of
  // the view then update accordingly.
  //
  // The default implementation does nothing.
  virtual void OnPropertiesChanged(
      fuchsia::ui::gfx::ViewProperties old_properties);

  // Called to handle an input event.
  // Returns true if the view will handle the event, false if the event
  // should continue propagating to other views which may handle it themselves.
  //
  // The default implementation does nothing but return false.
  //
  // TODO(SCN-804): This is not currently called; it needs to be hooked up.
  virtual bool OnInputEvent(fuchsia::ui::input::InputEvent event);

  // Called when a command sent by the client was not handled by Scenic.
  //
  // The default implementation does nothing.
  virtual void OnUnhandledCommand(fuchsia::ui::scenic::Command unhandled);

  // Called when an event that is not handled directly by BaseView is received.
  // For example, BaseView handles fuchsia::ui::gfx::ViewPropertiesChangedEvent,
  // and notifies the subclass via OnPropertiesChanged(); not all events are
  // handled in this way.
  //
  // The default implementation does nothing.
  virtual void OnEvent(fuchsia::ui::scenic::Event);

 private:
  // |scenic::SessionListener|
  //
  // Iterates over the received events and either handles them in a sensible way
  // (e.g. fuchsia::ui::gfx::ViewPropertiesChangedEvent is handled by invoking
  // the virtual method OnPropertiesChanged()), or delegates handling to the
  // subclass via the single-event version of OnEvent() above.
  //
  // Subclasses should not override this.
  void OnEvent(::fidl::VectorPtr<fuchsia::ui::scenic::Event> events) override;

  void PresentScene(zx_time_t presentation_time);

  fuchsia::sys::StartupContext* const startup_context_;
  fidl::Binding<fuchsia::ui::scenic::SessionListener> listener_binding_;
  Session session_;
  View view_;

  fuchsia::ui::gfx::vec3 logical_size_;

  fuchsia::ui::gfx::ViewProperties view_properties_;

  bool invalidate_pending_ = false;
  bool present_pending_ = false;

  zx_time_t last_presentation_time_ = 0;
};

}  // namespace scenic

#endif  // LIB_UI_SCENIC_CPP_BASE_VIEW_H_
