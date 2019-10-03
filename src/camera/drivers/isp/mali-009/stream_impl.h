// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_ISP_MALI_009_STREAM_IMPL_H_
#define SRC_CAMERA_DRIVERS_ISP_MALI_009_STREAM_IMPL_H_

#include <fuchsia/camera/common/cpp/fidl.h>
#include <fuchsia/sysmem/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding.h>
#include <zircon/types.h>

#include <unordered_set>

namespace camera {

class StreamImpl : public fuchsia::camera::common::Stream {
 public:
  StreamImpl() : binding_(this){};
  static zx_status_t Create(zx::channel channel, async_dispatcher_t* dispatcher,
                            StreamImpl** stream_out);

  void FrameAvailable(uint32_t id);

  bool IsBound() { return binding_.is_bound(); }

  const std::unordered_set<uint32_t>& GetOutstandingBuffers() { return outstanding_buffers_; }

  // |fuchsia::camera::common::Stream|
  virtual void Start() override;
  virtual void Stop() override;
  virtual void ReleaseFrame(uint32_t buffer_id) override;

 private:
  fidl::Binding<fuchsia::camera::common::Stream> binding_;
  std::unordered_set<uint32_t> outstanding_buffers_;
  bool streaming_ = false;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_ISP_MALI_009_STREAM_IMPL_H_
