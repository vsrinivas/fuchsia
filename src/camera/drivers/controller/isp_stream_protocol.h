// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_ISP_STREAM_PROTOCOL_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_ISP_STREAM_PROTOCOL_H_

#include <zircon/assert.h>

#include <ddktl/protocol/isp.h>

namespace camera {

// ISP Stream Protocol Implementation
class IspStreamProtocol {
 public:
  IspStreamProtocol() : protocol_{&protocol_ops_, nullptr} {}

  // Returns a pointer to this instance's protocol parameter, to be populated via the Stream banjo
  // interface.
  output_stream_protocol_t* protocol() { return &protocol_; }

  void Start() {
    ZX_ASSERT(ZX_OK == protocol_.ops->start(protocol_.ctx));
    started_ = true;
  }

  void Stop() {
    ZX_ASSERT(ZX_OK == protocol_.ops->stop(protocol_.ctx));
    started_ = false;
  }

  void ReleaseFrame(uint32_t buffer_id) const {
    ZX_ASSERT(ZX_OK == protocol_.ops->release_frame(protocol_.ctx, buffer_id));
  }

  zx_status_t Shutdown(const isp_stream_shutdown_callback_t* shutdown_callback) const {
    auto status = protocol_.ops->shutdown(protocol_.ctx, shutdown_callback);
    return status;
  }

 private:
  bool started_ = false;
  output_stream_protocol_t protocol_;
  output_stream_protocol_ops_t protocol_ops_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_ISP_STREAM_PROTOCOL_H_
