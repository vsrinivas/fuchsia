// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_SCENIC_CPP_SESSION_H_
#define LIB_UI_SCENIC_CPP_SESSION_H_

#include <lib/fit/function.h>
#include <lib/zx/event.h>

#include <fuchsia/images/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"

namespace scenic {

// Connect to Scenic and establish a new Session, as well as an InterfaceRequest
// for a SessionListener that can be hooked up as desired.
using SessionPtrAndListenerRequest =
    std::pair<fuchsia::ui::scenic::SessionPtr,
              fidl::InterfaceRequest<fuchsia::ui::scenic::SessionListener>>;
SessionPtrAndListenerRequest CreateScenicSessionPtrAndListenerRequest(
    fuchsia::ui::scenic::Scenic* scenic);

// Wraps a Scenic session.
// Maintains a queue of pending operations and assists with allocation of
// resource ids.
class Session : private fuchsia::ui::scenic::SessionListener {
 public:
  // Provides timing information about a presentation request which has
  // been applied by the scene manager.
  using PresentCallback =
      fit::function<void(fuchsia::images::PresentationInfo info)>;

  // Provide information about hits.
  using HitTestCallback =
      fit::function<void(fidl::VectorPtr<fuchsia::ui::gfx::Hit> hits)>;

  // Called when session events are received.
  using EventHandler =
      fit::function<void(fidl::VectorPtr<fuchsia::ui::scenic::Event>)>;

  // Wraps the provided session and session listener.
  // The listener is optional.
  explicit Session(fuchsia::ui::scenic::SessionPtr session,
                   fidl::InterfaceRequest<fuchsia::ui::scenic::SessionListener>
                       session_listener = nullptr);

  // Creates a new session using the provided Scenic and binds the listener to
  // this object. The Scenic itself is not retained after construction.
  explicit Session(fuchsia::ui::scenic::Scenic* scenic);

  explicit Session(SessionPtrAndListenerRequest session_and_listener);

  // Destroys the session.
  // All resources must be released prior to destruction.
  ~Session();

  // Sets a callback which is invoked if the session dies.
  void set_error_handler(fit::closure closure) {
    session_.set_error_handler(std::move(closure));
  }

  // Sets a callback which is invoked when events are received.
  void set_event_handler(EventHandler event_handler) {
    event_handler_ = std::move(event_handler);
  }

  // Gets a pointer to the underlying session interface.
  fuchsia::ui::scenic::Session* session() { return session_.get(); }

  // Allocates a new unique resource id.
  uint32_t AllocResourceId();

  // Enqueues an operation to release a resource.
  void ReleaseResource(uint32_t resource_id);

  // Enqueues an operation.
  // The session will queue operations locally to batch submission of operations
  // until |Flush()| or |Present()| is called.
  void Enqueue(fuchsia::ui::gfx::Command command);

  // Registers an acquire fence to be submitted during the subsequent call to
  // |Present()|.
  void EnqueueAcquireFence(zx::event fence);

  // Registers a release fence to be submitted during the subsequent call to
  // |Present()|.
  void EnqueueReleaseFence(zx::event fence);

  // Flushes queued operations to the session.
  void Flush();

  // Presents all previously enqueued operations.
  // Implicitly flushes all queued operations to the session.
  // Invokes the callback when the scene manager applies the presentation.
  void Present(uint64_t presentation_time, PresentCallback callback);

  // Performs a hit test along the specified ray.
  void HitTest(uint32_t node_id, const float ray_origin[3],
               const float ray_direction[3], HitTestCallback callback);

  // Performs a hit test along the specified ray into the engine's first
  // compositor.
  void HitTestDeviceRay(
      const float ray_origin[3], const float ray_direction[3],
      fuchsia::ui::scenic::Session::HitTestDeviceRayCallback callback);

 private:
  // |fuchsia::ui::scenic::SessionListener|
  void OnError(fidl::StringPtr error) override;
  void OnEvent(fidl::VectorPtr<fuchsia::ui::scenic::Event> events) override;

  fuchsia::ui::scenic::SessionPtr session_;
  uint32_t next_resource_id_ = 1u;
  uint32_t resource_count_ = 0u;

  fidl::VectorPtr<fuchsia::ui::scenic::Command> commands_;
  fidl::VectorPtr<zx::event> acquire_fences_;
  fidl::VectorPtr<zx::event> release_fences_;

  EventHandler event_handler_;
  fidl::Binding<fuchsia::ui::scenic::SessionListener> session_listener_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Session);
};

}  // namespace scenic

#endif  // LIB_UI_SCENIC_CPP_SESSION_H_
