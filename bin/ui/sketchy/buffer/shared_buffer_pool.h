// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SKETCHY_BUFFER_SHARED_BUFFER_POOL_H_
#define GARNET_BIN_UI_SKETCHY_BUFFER_SHARED_BUFFER_POOL_H_

#include <set>

#include "garnet/bin/ui/sketchy/buffer/shared_buffer.h"
#include "lib/escher/flib/fence_listener.h"
#include "lib/escher/vk/buffer_factory.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/ui/scenic/client/session.h"

namespace sketchy_service {

// Monitors used buffers and vendors free buffers for effective resource
// management and multi-buffering.
class SharedBufferPool final {
 public:
  SharedBufferPool(scenic::Session* session, escher::Escher* escher);

  // Gets a buffer with at least |capacity_req|.
  SharedBufferPtr GetBuffer(vk::DeviceSize capacity_req);

  // Returns a buffer to the pool because it is not used in the current canvas
  // state. |release_fence| is taken to monitor if the buffer could be recycled
  // to |free_buffers_|.
  void ReturnBuffer(SharedBufferPtr buffer, zx::event release_fence);

  // TODO(MZ-269): Implement CleanUp() to free up free_buffers_ a bit. It will
  // be useful when we support removing strokes.

  scenic::Session* session() const { return session_; }
  escher::Escher* escher() const { return escher_; }
  escher::BufferFactory* factory() const { return factory_.get(); }

 private:
  // Returns the key in |free_buffers_| in which to find a buffer with at least
  // |capacity_req|.
  vk::DeviceSize GetBufferKey(vk::DeviceSize capacity_req);

  // Recycles the buffer to |free_buffers_| for future use.
  void RecycleBuffer(SharedBufferPtr buffer);

  scenic::Session* session_;
  escher::Escher* escher_;
  std::unique_ptr<escher::BufferFactory> factory_;
  std::set<SharedBufferPtr> used_buffers_;
  // Groups free buffers into lists that contain buffers that have the same
  // capacity.
  std::map<vk::DeviceSize, std::vector<SharedBufferPtr>> free_buffers_;
  // Extends the lifetime of fence listeners until they are signaled.
  std::set<std::unique_ptr<escher::FenceListener>> fence_listeners_;

  fxl::WeakPtrFactory<SharedBufferPool> weak_factory_;  // must be last
};

}  // namespace sketchy_service

#endif  // GARNET_BIN_UI_SKETCHY_BUFFER_SHARED_BUFFER_POOL_H_
