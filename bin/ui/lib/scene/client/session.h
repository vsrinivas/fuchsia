// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/services/scene/session.fidl.h"

#include <functional>

#include <mx/event.h>

#include "lib/ftl/macros.h"

namespace mozart {
namespace client {

// Wraps a Mozart session.
// Maintains a queue of pending operations and assists with allocation of
// resource ids.
class Session {
 public:
  // Provides timing information about a presentation request which has
  // been applied by the scene manager.
  using PresentCallback =
      std::function<void(mozart2::PresentationInfoPtr info)>;

  // Provide information about hits.
  using HitTestCallback =
      std::function<void(fidl::Array<mozart2::HitPtr> hits)>;

  explicit Session(mozart2::SessionPtr session);
  ~Session();

  // Sets a callback which is invoked if the session dies.
  void set_connection_error_handler(ftl::Closure closure) {
    session_.set_connection_error_handler(std::move(closure));
  }

  // Gets a pointer to the underlying session interface.
  mozart2::Session* session() { return session_.get(); }

  // Allocates a new unique resource id.
  uint32_t AllocResourceId();

  // Enqueues an operation to release a resource.
  void ReleaseResource(uint32_t resource_id);

  // Enqueues an operation.
  // The session will queue operations locally to batch submission of operations
  // until |Flush()| or |Present()| is called.
  void Enqueue(mozart2::OpPtr op);

  // Registers an acquire fence to be submitted during the subsequent call to
  // |Present()|.
  void EnqueueAcquireFence(mx::event fence);

  // Registers a release fence to be submitted during the subsequent call to
  // |Present()|.
  void EnqueueReleaseFence(mx::event fence);

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

 private:
  mozart2::SessionPtr session_;
  uint32_t next_resource_id_ = 1u;
  uint32_t resource_count_ = 0u;

  fidl::Array<mozart2::OpPtr> ops_;
  fidl::Array<mx::event> acquire_fences_;
  fidl::Array<mx::event> release_fences_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Session);
};

}  // namespace client
}  // namespace mozart
