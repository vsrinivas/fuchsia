// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_SCENIC_CLIENT_SESSION_H_
#define LIB_UI_SCENIC_CLIENT_SESSION_H_

#include <zx/event.h>

#include <functional>

#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/ui/scenic/fidl/scenic.fidl.h"
#include "lib/ui/scenic/fidl/session.fidl.h"

#include "lib/ui/gfx/fidl/display_info.fidl.h"

namespace scenic_lib {

// Wraps a Scenic session.
// Maintains a queue of pending operations and assists with allocation of
// resource ids.
class Session : private ui::SessionListener {
 public:
  // Provides timing information about a presentation request which has
  // been applied by the scene manager.
  using PresentCallback = std::function<void(ui::PresentationInfoPtr info)>;

  // Provide information about hits.
  using HitTestCallback = std::function<void(f1dl::Array<ui::gfx::HitPtr> hits)>;

  // Called when session events are received.
  using EventHandler = std::function<void(f1dl::Array<ui::EventPtr>)>;

  // Wraps the provided session and session listener.
  // The listener is optional.
  explicit Session(
      ui::SessionPtr session,
      f1dl::InterfaceRequest<ui::SessionListener> session_listener = nullptr);

  // Creates a new session using the provided scene manager and binds the
  // session listener to this object.
  // The scene manager itself is not retained after construction.
  explicit Session(ui::Scenic* mozart);

  // Destroys the session.
  // All resources must be released prior to destruction.
  ~Session();

  // Sets a callback which is invoked if the session dies.
  void set_error_handler(fxl::Closure closure) {
    session_.set_error_handler(std::move(closure));
  }

  // Sets a callback which is invoked when events are received.
  void set_event_handler(EventHandler event_handler) {
    event_handler_ = std::move(event_handler);
  }

  // Gets a pointer to the underlying session interface.
  ui::Session* session() { return session_.get(); }

  // Allocates a new unique resource id.
  uint32_t AllocResourceId();

  // Enqueues an operation to release a resource.
  void ReleaseResource(uint32_t resource_id);

  // Enqueues an operation.
  // The session will queue operations locally to batch submission of operations
  // until |Flush()| or |Present()| is called.
  void Enqueue(ui::gfx::CommandPtr command);

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
  void HitTest(uint32_t node_id,
               const float ray_origin[3],
               const float ray_direction[3],
               HitTestCallback callback);

  // Performs a hit test along the specified ray into the engine's first
  // compositor.
  void HitTestDeviceRay(const float ray_origin[3],
                        const float ray_direction[3],
                        const ui::Session::HitTestDeviceRayCallback& callback);

 private:
  // |ui::SessionListener|
  void OnError(const f1dl::String& error) override;
  void OnEvent(f1dl::Array<ui::EventPtr> events) override;

  ui::SessionPtr session_;
  uint32_t next_resource_id_ = 1u;
  uint32_t resource_count_ = 0u;

  f1dl::Array<ui::CommandPtr> commands_;
  f1dl::Array<zx::event> acquire_fences_;
  f1dl::Array<zx::event> release_fences_;

  EventHandler event_handler_;
  f1dl::Binding<ui::SessionListener> session_listener_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Session);
};

}  // namespace scenic_lib

#endif  // LIB_UI_SCENIC_CLIENT_SESSION_H_
