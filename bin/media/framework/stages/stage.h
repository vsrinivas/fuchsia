// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "apps/media/src/framework/packet.h"
#include "apps/media/src/framework/payload_allocator.h"
#include "apps/media/src/framework/stages/input.h"
#include "apps/media/src/framework/stages/output.h"

namespace mojo {
namespace media {

class Engine;

// Host for a source, sink or transform.
class Stage {
 public:
  using UpstreamCallback = std::function<void(size_t input_index)>;
  using DownstreamCallback = std::function<void(size_t output_index)>;
  using UpdateCallback = std::function<void(Stage* stage)>;

  Stage();

  virtual ~Stage();

  void SetUpdateCallback(const UpdateCallback& update_callback) {
    update_callback_ = update_callback;
  }

  // Returns the number of input connections.
  virtual size_t input_count() const = 0;

  // Returns the indicated input connection.
  virtual Input& input(size_t index) = 0;

  // Returns the number of output connections.
  virtual size_t output_count() const = 0;

  // Returns the indicated output connection.
  virtual Output& output(size_t index) = 0;

  // Prepares the input for operation. Returns nullptr unless the connected
  // output must use a specific allocator, in which case it returns that
  // allocator.
  virtual PayloadAllocator* PrepareInput(size_t index) = 0;

  // Prepares the output for operation, passing an allocator that must be used
  // by the output or nullptr if there is no such requirement. The callback is
  // used to indicate what inputs are ready to be prepared as a consequence of
  // preparing the output.
  virtual void PrepareOutput(size_t index,
                             PayloadAllocator* allocator,
                             const UpstreamCallback& callback) = 0;

  // Unprepares the input. The default implementation does nothing.
  virtual void UnprepareInput(size_t index);

  // Unprepares the output. The default implementation does nothing. The
  // the callback is used to indicate what inputs are ready to be unprepared as
  // a consequence of unpreparing the output.
  virtual void UnprepareOutput(size_t index, const UpstreamCallback& callback);

  // Performs processing.
  virtual void Update(Engine* engine) = 0;

  // Flushes an input. The callback is used to indicate what outputs are ready
  // to be flushed as a consequence of flushing the input.
  virtual void FlushInput(size_t index, const DownstreamCallback& callback) = 0;

  // Flushes an output.
  virtual void FlushOutput(size_t index) = 0;

 protected:
  void RequestUpdate() {
    FTL_DCHECK(update_callback_);
    update_callback_(this);
  }

 private:
  UpdateCallback update_callback_;
  bool in_supply_backlog_;
  bool in_demand_backlog_;

  friend class Engine;
};

}  // namespace media
}  // namespace mojo
